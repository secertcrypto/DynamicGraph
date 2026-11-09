#!/usr/bin/env python3
"""Local two- or three-process benchmark for the real CrIS-MPC dynamic graph engine."""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import queue
import random
import statistics
import subprocess
import sys
import tempfile
import threading
import time


def options():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, default=Path("build/benchmark/bench_dynamic_graph"))
    p.add_argument("--backend", choices=["nph", "two_party"], default="nph",
                   help="whole runtime: two_party launches P0/P1 only")
    p.add_argument("--init-backend",choices=["nph","two_party"],default=None)
    p.add_argument("--vertices", type=int, default=16)
    p.add_argument("--base-edges", "--initial-edges", type=int, default=24)
    p.add_argument("--rounds", type=int, help="default 3, or the length of an explicit schedule")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--insert-per-round", type=int, help="same number each round (default 1)")
    g.add_argument("--insert-schedule", help="comma-separated counts, e.g. 0,2,0,1")
    g.add_argument("--insert-file", type=Path, help='public JSON: one list of [id,src,dst,weight] per round')
    p.add_argument("--max-delta", type=int, help="default total insertions")
    p.add_argument("--shift", type=int, default=0, help="0 = engine auto mode")
    p.add_argument("--application", choices=["weighted_sum","reachability"], default="weighted_sum")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--repeat", type=int, default=3)
    p.add_argument("--warmup", type=int, default=0)
    p.add_argument("--threads", type=int, default=1)
    p.add_argument("--port", type=int, default=19500)
    p.add_argument("--timeout", type=float, default=300, help="seconds per trial")
    p.add_argument("--check", action="store_true", help="validate public fixture after measurement")
    p.add_argument("--out", type=Path, help="new output directory")
    a=p.parse_args()
    if a.backend=="two_party" and a.init_backend=="nph":
        p.error("--backend two_party requires two_party initialization")
    a.init_backend=a.init_backend or ("two_party" if a.backend=="two_party" else "nph")
    a.parties=2 if a.backend=="two_party" else 3
    return a


def workload(a):
    if not 1 <= a.vertices <= (1 << 22) or not 0 <= a.base_edges <= (1<<31)-1-a.vertices:
        raise ValueError("invalid vertices/base-edges")
    if a.repeat<1 or a.warmup<0 or a.threads<1 or a.timeout<=0 or not 1024<=a.port<=65535-2049:
        raise ValueError("invalid repetition/thread/timeout/port setting")
    source = random.Random(a.seed)
    n = a.vertices
    def edge():
        return (source.randrange(n), source.randrange(n), source.randint(-3,5))
    initial = ([1]+[0]*(n-1) if a.application=="reachability"
               else [source.randint(-3,5) for _ in range(n)])
    base = [edge() for _ in range(a.base_edges)]
    if a.insert_file:
        rounds = json.loads(a.insert_file.read_text())
        if not isinstance(rounds,list): raise ValueError("insert file must contain a list of rounds")
        ids=set()
        for row in rounds:
            if not isinstance(row,list): raise ValueError("expected list of edges for each round")
            for item in row:
                if not isinstance(item,list) or len(item)!=4 or not all(type(x) is int for x in item):
                    raise ValueError("expected [id,src,dst,weight] with integer entries")
                eid,src,dst,_=item
                if not 0<=eid<2**64 or eid in ids or not (0<=src<n and 0<=dst<n):
                    raise ValueError("invalid endpoint, duplicate or invalid edge ID")
                ids.add(eid)
    else:
        if a.insert_schedule is not None:
            counts=[int(v) for v in a.insert_schedule.split(",")]
        else:
            count=1 if a.insert_per_round is None else a.insert_per_round
            counts=[count]*(3 if a.rounds is None else a.rounds)
        if any(c<0 for c in counts): raise ValueError("negative edge count")
        rounds=[]; eid=0
        for c in counts:
            row=[]
            for _ in range(c):
                row.append((eid,*edge()));eid+=1
            rounds.append(row)
    if not rounds or (a.rounds is not None and len(rounds)!=a.rounds):
        raise ValueError("--rounds must be positive and match schedule length")
    total=sum(map(len,rounds))
    cap=total if a.max_delta is None else a.max_delta
    if not total<=cap<1<<62: raise ValueError("max-delta must cover all insertions and be below 2^62")
    ell=a.shift or max(1,(2*cap).bit_length())
    if not 1<=ell<=63 or (1<<ell)<=2*cap: raise ValueError("require 2^shift > 2*max-delta")
    q=1<<(64-ell)
    if a.application=="reachability" and a.base_edges+cap>=q:
        raise ValueError("reachability requires base-edges + max-delta < q")
    return initial,base,rounds,cap,ell,q


def prepare(a,d,initial,base,rounds,cap,q):
    # Reproducible public fixtures, not production secret-sharing randomness.
    split_rng=random.Random(a.seed ^ 0xCB1)
    def split(x,mod):
        r=split_rng.randrange(mod)
        return r,(x-r)%mod
    common=dict(backend=a.backend,two_party_init=a.init_backend=="two_party",vertices=a.vertices,base_edges=len(base),max_delta=cap,shift=a.shift,application=a.application)
    inputs=[dict(common,rounds=[]) for _ in range(a.parties)]
    for p in (0,1): inputs[p]["base_rows"]=[]
    for src,dst,w in [(v,v,h) for v,h in enumerate(initial)]+base:
        parts=[split(src,a.vertices),split(dst,a.vertices),split(w,q)]
        for p in (0,1):inputs[p]["base_rows"].append([val[p] for val in parts])
    for row in rounds:
        inserts=[[] for _ in range(a.parties)]
        for eid,src,dst,w in row:
            s,dst_s,ws=split(src,a.vertices),split(dst,a.vertices),split(w,q)
            for p in (0,1):inserts[p].append(dict(id=eid,src=s[p],dst=dst_s[p],data=ws[p]))
            if a.parties==3: inserts[2].append(dict(id=eid))
        for p in range(a.parties): inputs[p]["rounds"].append(dict(insert=inserts[p]))
    for p,obj in enumerate(inputs):(d/f"in-{p}.json").write_text(json.dumps(obj,separators=(",",":")))


def verify(a,d,initial,base,rounds,q):
    rows=[(d/f"out-{p}.jsonl").read_text().splitlines() for p in range(a.parties)]
    if any(len(r)!=len(rounds) for r in rows): raise ValueError("missing output rounds")
    h=[x%q for x in initial];active=list(base)
    for t,additions in enumerate(rounds):
        active.extend(edge[1:] for edge in additions)
        if a.application=="reachability":
            nxt=[int(bool(x)) for x in h]
            for src,dst,_ in active:
                if h[src]:nxt[dst]=1
        else:
            nxt=h.copy()
            for src,dst,w in active:nxt[dst]=(nxt[dst]+h[src]*w)%q
        h=nxt
        decoded=[json.loads(r[t]) for r in rows]
        p0,p1=decoded[:2]
        helper=decoded[2] if a.parties==3 else []
        if helper or len(p0)!=len(h) or len(p1)!=len(h) or [(x+y)%q for x,y in zip(p0,p1)]!=h:
            raise ValueError(f"round {t}: reconstructed output != reference")


def collect_stats(party_rows,phase,t,new,active,a,rep):
    if any((r["phase"],r["round"],r["inserted"],r["active"])!=(phase,t,new,active) for r in party_rows):
        raise ValueError("inconsistent worker phase metadata")
    for i in range(a.parties):
        for j in range(a.parties):
            if party_rows[i]["nph_tx"][j]!=party_rows[j]["nph_rx"][i]:
                raise ValueError(f"NPH payload count mismatch {i}->{j}")
    if a.parties==3 and (party_rows[2]["risa_tx"]!=0 or party_rows[2]["two_party_tx"]!=0): raise ValueError("helper sent RISA traffic")
    nph=sum(sum(r["nph_tx"]) for r in party_rows)
    helper=(sum(r["nph_tx"][2] for r in party_rows[:2])+sum(party_rows[2]["nph_tx"])) if a.parties==3 else 0
    if a.parties==2 and any(any(r[k]) for r in party_rows for k in ("nph_tx","nph_rx")):
        raise ValueError("pure two-party runtime reported NPH traffic")
    twopc=sum(r["two_party_tx"] for r in party_rows)
    risa=sum(r["risa_tx"] for r in party_rows)
    return dict(repeat=rep,phase=phase,round=t,vertices=a.vertices,base_edges=a.base_edges,
                inserted=new,active_delta=active,ms=max(r["ms"] for r in party_rows),
                nph_bytes=nph,nph_helper_bytes=helper,risa_bytes=risa,two_party_bytes=twopc,total_bytes=nph+risa+twopc)


def trial(a,d,rounds,rep,logdir,plan=None):
    procs=[];threads=[];logs=[];events=queue.Queue();stages=[];raw=[]
    deadline=time.monotonic()+a.timeout
    def pump(p,pipe,log):
        try:
            for line in pipe:
                if line.startswith("@DG "): events.put((p,line[4:].strip()))
                else: log.write(line);log.flush()
        finally: events.put((p,None))
    def wait_group(ready=False, final=False):
        results={}
        while len(results)<a.parties:
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError("trial timed out")
            try:p,s=events.get(timeout=remaining)
            except queue.Empty:raise TimeoutError("trial timed out") from None
            if s is None and final and p in results: continue
            if s is None:raise RuntimeError(f"P{p} exited unexpectedly; see {logdir}")
            if p in results:raise RuntimeError("duplicate worker event")
            if ready:
                if s!="READY":raise RuntimeError("worker not ready")
                results[p]=s
            else:
                results[p]=json.loads(s)
                if results[p]["pid"]!=p:raise RuntimeError("wrong reporting party")
        return [results[i] for i in range(a.parties)]
    try:
        for p in range(a.parties):
            log=(logdir/f"p{p}.log").open("w");logs.append(log)
            cmd=[str(a.binary.resolve()),str(p),str(a.port),str(d/f"in-{p}.json"),
                 str(d/f"out-{p}.jsonl") if a.check else "-","--worker"]
            proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                  text=True,bufsize=1,env=dict(os.environ,OMP_NUM_THREADS=str(a.threads)))
            procs.append(proc)
            thread=threading.Thread(target=pump,args=(p,proc.stdout,log),daemon=True)
            threads.append(thread);thread.start()
        wait_group(ready=True)
        if plan is None:
            plan=[("init",-1,0,0)];active=0
            for t,row in enumerate(rounds):
                active+=len(row)
                plan.extend([("insert",t,len(row),active),("iter",t,0,active)])
        for phase,t,new,active in plan:
            for p in procs:p.stdin.write("GO\n");p.stdin.flush()
            metrics=wait_group(final=(phase=="iter" and t+1==len(rounds)))
            stages.append(collect_stats(metrics,phase,t,new,active,a,rep))
            raw.extend(dict(repeat=rep,**r) for r in metrics)
        for p in procs:
            if p.wait(timeout=max(.01,deadline-time.monotonic()))!=0:raise RuntimeError("worker failed")
        return stages,raw
    finally:
        for p in procs:
            if p.poll() is None:p.kill()
            p.wait();p.stdin.close()
        for thread in threads:thread.join()
        for p in procs:p.stdout.close()
        for log in logs:log.close()


def csv_write(path,rows):
    with path.open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)


def main():
    a=options()
    if not a.binary.is_file():raise ValueError(f"build bench_dynamic_graph first: {a.binary}")
    initial,base,rounds,cap,ell,q=workload(a)
    out=a.out or Path("Results")/"dynamic_graph"/dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    out.mkdir(parents=True,exist_ok=False)
    meta=dict(schema_version=2,backend=a.backend,num_processes=a.parties,init_backend=a.init_backend,vertices=a.vertices,base_edges=a.base_edges,insert_schedule=list(map(len,rounds)),seed=a.seed,
              application=a.application,max_delta=cap,shift=ell,q=q,check=a.check,threads=a.threads,
              warmup=a.warmup,repeat=a.repeat,platform=platform.platform(),binary=str(a.binary.resolve()),
              workload_sha256=hashlib.sha256(json.dumps([initial,base,rounds]).encode()).hexdigest())
    (out/"metadata.json").write_text(json.dumps(meta,indent=2))
    samples=[];parties=[]
    with tempfile.TemporaryDirectory(prefix="cris-dg-bench-") as tmp:
        d=Path(tmp);prepare(a,d,initial,base,rounds,cap,q)
        for rep in range(-a.warmup,a.repeat):
            logs=out/f"trial-{rep}";logs.mkdir()
            data,raw=trial(a,d,rounds,rep,logs)
            if a.check: verify(a,d,initial,base,rounds,q)
            if rep>=0:
                samples.extend(data);parties.extend(raw)
                csv_write(out/"samples.csv",samples)
                (out/"parties.jsonl").write_text("".join(json.dumps(r)+"\n" for r in parties))
    summary=[]
    def add(identity,group):
        m={**identity}
        for key in ("ms","nph_bytes","nph_helper_bytes","risa_bytes","two_party_bytes","total_bytes"):
            m["median_"+key]=statistics.median(r[key] for r in group)
        summary.append(m)
    fields=("phase","round","inserted","active_delta")
    for key in dict.fromkeys(tuple(r[f] for f in fields) for r in samples):
        add(dict(zip(fields,key)),[r for r in samples if tuple(r[f] for f in fields)==key])
    totals=[]
    for rep in range(a.repeat):
        totals.append({k:sum(r[k] for r in samples if r["repeat"]==rep)
                       for k in ("ms","nph_bytes","nph_helper_bytes","risa_bytes","two_party_bytes","total_bytes")})
    add(dict(phase="total",round=-1,inserted=sum(map(len,rounds)),active_delta=sum(map(len,rounds))),totals)
    csv_write(out/"summary.csv",summary)
    print("phase    t    new active   median_ms     NPH_bytes     TWO_PC_bytes     RISA_bytes    total_bytes")
    for r in summary:
        print(f'{r["phase"]:6} {r["round"]:3} {r["inserted"]:5} {r["active_delta"]:6}'
              f' {r["median_ms"]:11.3f} {r["median_nph_bytes"]:13.0f} {r["median_two_party_bytes"]:16.0f} {r["median_risa_bytes"]:14.0f}'
              f' {r["median_total_bytes"]:14.0f}')
    print(f"Results: {out}" + (" (outputs verified)" if a.check else ""))

if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        sys.exit(f"benchmark: {exc}")
