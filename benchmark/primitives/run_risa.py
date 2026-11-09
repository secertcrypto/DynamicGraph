#!/usr/bin/env python3
"""Two-party RISA primitive benchmark, with separate timing and traffic per phase."""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import queue
import random
import statistics
import subprocess
import sys
import tempfile
import threading
import time

MOD=1<<64
MASK=MOD-1


def options():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary",type=Path,default=Path("build/benchmark/bench_risa"))
    p.add_argument("--database-size","--db-size",type=int,default=64,help="fixed number of uint64 entries")
    g=p.add_mutually_exclusive_group()
    g.add_argument("--batch-size",type=int,help="compile once before round 0; default 4")
    g.add_argument("--insert-per-round",type=int,help="compile this many new address states each round")
    g.add_argument("--insert-schedule",help="new state counts, e.g. 4,2,0,1")
    p.add_argument("--rounds",type=int,help="default 3, or schedule length")
    p.add_argument("--update-batch",type=int,help="update first min(B,active) states; default all")
    p.add_argument("--read-batch",type=int,help="read first min(B,active) states; default all")
    p.add_argument("--seed",type=int,default=42)
    p.add_argument("--repeat",type=int,default=3)
    p.add_argument("--warmup",type=int,default=0)
    p.add_argument("--threads",type=int,default=1)
    p.add_argument("--port",type=int,default=23100)
    p.add_argument("--timeout",type=float,default=300,help="seconds per trial")
    p.add_argument("--check",action="store_true",help="verify update error bounds and exact reads outside timing")
    p.add_argument("--out",type=Path,help="new output directory")
    a=p.parse_args()
    if not 1<=a.database_size<=1<<22:p.error("database size must be in [1, 2^22]")
    if a.rounds is not None and a.rounds<1:p.error("rounds must be positive")
    if a.insert_schedule is not None:
        try:counts=[int(x) for x in a.insert_schedule.split(',')]
        except ValueError:p.error("insert schedule must contain comma-separated integers")
        if a.rounds is not None and len(counts)!=a.rounds:p.error("rounds must match schedule length")
    else:
        k=a.rounds or 3
        counts=([a.insert_per_round]*k if a.insert_per_round is not None else
                [4 if a.batch_size is None else a.batch_size]+[0]*(k-1))
    if not counts or any(x<0 for x in counts):p.error("insertion counts must be nonnegative")
    if any(x is not None and x<0 for x in (a.update_batch,a.read_batch)):p.error("batch limits must be nonnegative")
    if a.repeat<1 or a.warmup<0 or a.threads<1 or a.timeout<=0 or not 1024<=a.port<=65535:
        p.error("invalid repetition/thread/timeout/port setting")
    a.counts=counts;a.rounds=len(counts)
    return a


def fixture(a):
    rng=random.Random(a.seed)
    database=[rng.getrandbits(64) for _ in range(a.database_size)]
    rounds=[];active=0
    for count in a.counts:
        indices=[rng.randrange(a.database_size) for _ in range(count)]
        active+=count
        u=active if a.update_batch is None else min(active,a.update_batch)
        r=active if a.read_batch is None else min(active,a.read_batch)
        rounds.append(dict(indices=indices,messages=[rng.randint(-10,10)%MOD for _ in range(u)],read_count=r))
    return database,rounds


def prepare(a,d,database,rounds):
    rng=random.Random(a.seed ^ 0x52495341)
    def split(x,mod):
        first=rng.randrange(mod);return first,(x-first)%mod
    def split_array(values,mod):return list(zip(*(split(x,mod) for x in values))) if values else [[],[]]
    databases=split_array(database,MOD)
    inputs=[dict(database_size=a.database_size,database=databases[p],rounds=[]) for p in range(2)]
    for row in rounds:
        indices=split_array(row["indices"],a.database_size);messages=split_array(row["messages"],MOD)
        for p in range(2):inputs[p]["rounds"].append(dict(indices=indices[p],messages=messages[p],read_count=row["read_count"]))
    for p,item in enumerate(inputs):(d/f"in-{p}.json").write_text(json.dumps(item,separators=(',',':')))


def phase_plan(rounds):
    plan=[(name,-1,0,0) for name in ("connect","setup_update","setup_read")]
    active=0
    for t,row in enumerate(rounds):
        new=len(row["indices"]);active+=new
        for name,batch in (("compile_read",new),("compile_update",new),
                           ("update",len(row["messages"])),("apply_update",len(row["messages"])),
                           ("read",row["read_count"])):
            plan.append((name,t,batch,active))
    return plan


def trial(a,d,rounds,rep,logdir):
    procs=[];threads=[];logs=[];events=queue.Queue();samples=[];raw=[]
    deadline=time.monotonic()+a.timeout
    def pump(pid,pipe,log):
        try:
            for line in pipe:
                if line.startswith("@RISA "):events.put((pid,line[6:].strip()))
                else:log.write(line);log.flush()
        finally:events.put((pid,None))
    def wait_group(ready=False,final=False):
        result={}
        while len(result)<2:
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError("trial timed out")
            try:pid,line=events.get(timeout=remaining)
            except queue.Empty:raise TimeoutError("trial timed out") from None
            if line is None and final and pid in result:continue
            if line is None:raise RuntimeError(f"P{pid} exited unexpectedly; see {logdir}")
            if pid in result:raise RuntimeError("duplicate worker event")
            if ready:
                if line!="READY":raise RuntimeError("worker not ready")
                result[pid]=line
            else:
                result[pid]=json.loads(line)
                if result[pid]["pid"]!=pid:raise RuntimeError("wrong reporting party")
        return [result[p] for p in range(2)]
    try:
        for p in range(2):
            log=(logdir/f"p{p}.log").open('w');logs.append(log)
            proc=subprocess.Popen([str(a.binary.resolve()),str(p),str(a.port),str(d/f"in-{p}.json"),
                str(d/f"check-{p}.jsonl") if a.check else "-","--worker"],stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1,
                env=dict(os.environ,OMP_NUM_THREADS=str(a.threads)))
            procs.append(proc)
            thread=threading.Thread(target=pump,args=(p,proc.stdout,log),daemon=True)
            threads.append(thread);thread.start()
        wait_group(ready=True)
        plan=phase_plan(rounds)
        for i,(phase,t,batch,active) in enumerate(plan):
            for proc in procs:proc.stdin.write("GO\n");proc.stdin.flush()
            metrics=wait_group(final=i==len(plan)-1)
            for r in metrics:
                if (r["phase"],r["round"],r["batch_size"],r["active_states"])!=(phase,t,batch,active):
                    raise RuntimeError("worker phase metadata mismatch")
                if r["bytes_sent"]<0:raise RuntimeError("invalid byte count")
            sent=sum(r["bytes_sent"] for r in metrics)
            if phase=="update" and any(r["bytes_sent"]!=8*batch for r in metrics):
                raise RuntimeError("update payload count mismatch")
            if phase=="read" and any(r["bytes_sent"]!=batch*r["read_response_bytes"] for r in metrics):
                raise RuntimeError("PIR response payload count mismatch")
            samples.append(dict(repeat=rep,phase=phase,round=t,database_size=a.database_size,
                batch_size=batch,active_states=active,ms=max(r["ms"] for r in metrics),
                p0_tx=metrics[0]["bytes_sent"],p1_tx=metrics[1]["bytes_sent"],total_bytes=sent))
            raw.extend(dict(repeat=rep,**r) for r in metrics)
        for proc in procs:
            if proc.wait(timeout=max(.01,deadline-time.monotonic()))!=0:raise RuntimeError("worker failed")
        return samples,raw
    finally:
        for proc in procs:
            if proc.poll() is None:proc.kill()
            proc.wait();proc.stdin.close()
        for thread in threads:thread.join()
        for proc in procs:proc.stdout.close()
        for log in logs:log.close()


def verify(d,database,rounds):
    outputs=[[json.loads(line) for line in (d/f"check-{p}.jsonl").read_text().splitlines()] for p in range(2)]
    if any(len(rows)!=len(rounds) for rows in outputs):raise ValueError("missing check rounds")
    indices=[];current=list(database)
    for t,row in enumerate(rounds):
        indices.extend(row["indices"]);a,b=outputs[0][t],outputs[1][t]
        if any(r["round"]!=t or r["error_bound"]!=len(row["messages"]) for r in (a,b)):
            raise ValueError("invalid update error-bound metadata")
        for key,length in (("delta",len(database)),("database",len(database)),("read",row["read_count"])):
            if len(a[key])!=length or len(b[key])!=length:raise ValueError("wrong check vector length")
        delta=[(x+y)&MASK for x,y in zip(a["delta"],b["delta"])]
        expected=[0]*len(database)
        for index,message in zip(indices,row["messages"]):expected[index]=(expected[index]+message)&MASK
        for got,want in zip(delta,expected):
            err=(got-want)&MASK
            if min(err,(-err)&MASK)>len(row["messages"]):raise ValueError(f"round {t}: update exceeds error bound")
        current=[(x+y)&MASK for x,y in zip(current,delta)]
        observed=[(x+y)&MASK for x,y in zip(a["database"],b["database"])]
        if current!=observed:raise ValueError(f"round {t}: database accumulation mismatch")
        reads=[(x+y)&MASK for x,y in zip(a["read"],b["read"])]
        if reads!=[current[index] for index in indices[:row["read_count"]]]:
            raise ValueError(f"round {t}: read differs from actual updated database")


def csv_write(path,rows):
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)


def main():
    a=options()
    if not a.binary.is_file():raise ValueError(f"build bench_risa first: {a.binary}")
    database,rounds=fixture(a)
    out=a.out or Path("Results")/"risa"/dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    out.mkdir(parents=True,exist_ok=False)
    meta=dict(schema_version=1,benchmark="risa_primitives",num_processes=2,database_size=a.database_size,
        word_bits=64,insert_schedule=a.counts,update_batch=a.update_batch,read_batch=a.read_batch,
        rounds=a.rounds,seed=a.seed,repeat=a.repeat,warmup=a.warmup,threads=a.threads,
        check=a.check,correctness_checked=False,update_semantics="approximate_additive",
        read_semantics="exact_actual_database",compile_order=["read","update"],
        binary=str(a.binary.resolve()),
        workload_sha256=hashlib.sha256(json.dumps([database,rounds]).encode()).hexdigest())
    (out/"metadata.json").write_text(json.dumps(meta,indent=2))
    samples=[];parties=[]
    with tempfile.TemporaryDirectory(prefix="cris-risa-bench-") as tmp:
        d=Path(tmp);prepare(a,d,database,rounds)
        for rep in range(-a.warmup,a.repeat):
            logdir=out/f"trial-{rep}";logdir.mkdir()
            data,raw=trial(a,d,rounds,rep,logdir)
            if a.check:verify(d,database,rounds)
            if rep>=0:
                samples.extend(data);parties.extend(raw)
                csv_write(out/"samples.csv",samples)
                (out/"parties.jsonl").write_text(''.join(json.dumps(r)+'\n' for r in parties))
    keys=("ms","p0_tx","p1_tx","total_bytes")
    def med(group):return {"median_"+key:statistics.median(row[key] for row in group) for key in keys}
    def totals(phase=None):
        return [{key:sum(r[key] for r in samples if r["repeat"]==rep and (phase is None or r["phase"]==phase))
                 for key in keys} for rep in range(a.repeat)]
    summary=[dict(phase=phase,round=t,batch_size=batch,active_states=active,
                  **med([r for r in samples if r["phase"]==phase and r["round"]==t]))
             for phase,t,batch,active in phase_plan(rounds)]
    summary.append(dict(phase="total",round=-1,batch_size=0,active_states=sum(a.counts),**med(totals())))
    csv_write(out/"summary.csv",summary)
    phase_totals=[dict(phase=phase,**med(totals(phase))) for phase in dict.fromkeys(r["phase"] for r in samples)]
    csv_write(out/"phase_totals.csv",phase_totals)
    meta.update(correctness_checked=a.check,read_response_bytes=parties[0]["read_response_bytes"])
    (out/"metadata.json").write_text(json.dumps(meta,indent=2))
    print("phase             t  batch active    median_ms       P0_bytes       P1_bytes    total_bytes")
    for r in summary:
        print(f'{r["phase"]:16} {r["round"]:3} {r["batch_size"]:6} {r["active_states"]:6}'
              f' {r["median_ms"]:12.3f} {r["median_p0_tx"]:14.0f} {r["median_p1_tx"]:14.0f} {r["median_total_bytes"]:14.0f}')
    print(f"Results: {out}"+(" (update error bounds and reads verified)" if a.check else ""))


if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        sys.exit(f"benchmark: {exc}")
