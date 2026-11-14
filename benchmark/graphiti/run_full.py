#!/usr/bin/env python3
"""Benchmark real two-party Graphiti initialization and K static graph iterations."""
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile

# Reuse the dynamic benchmark's fixture generation, sharing, oracle, process
# coordination and statistics conventions so matching inputs mean the same work.
_path=Path(__file__).resolve().parents[1]/"dynamic_graph"/"run.py"
_spec=importlib.util.spec_from_file_location("dynamic_benchmark",_path)
common=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)


def options(rebuild=False):
    p=argparse.ArgumentParser(description=("Graphiti full-rebuild baseline with inter-round edge insertions."
                                          if rebuild else __doc__))
    p.add_argument("--binary",type=Path,default=Path("build/benchmark/bench_FullGraphiti"))
    p.add_argument("--vertices","--num-verts",type=int,default=16)
    p.add_argument("--edges","--base-edges","--num-edges",dest="base_edges",type=int,default=24)
    p.add_argument("--rounds","--num-iters",type=int,default=None if rebuild else 3)
    p.add_argument("--application",choices=["weighted_sum","reachability"],default="weighted_sum")
    p.add_argument("--max-delta",type=int,default=None if rebuild else 0,
                   help="insertion bound/value-ring configuration; defaults to total insertions")
    if rebuild:
        group=p.add_mutually_exclusive_group()
        group.add_argument("--insert-per-round",type=int,help="default 1")
        group.add_argument("--insert-schedule",help="comma-separated counts, e.g. 0,2,0,1")
        group.add_argument("--insert-file",type=Path,help="public per-round [id,src,dst,weight] lists")
        p.add_argument("--reinit-policy",choices=["every_round","on_insert"],default="on_insert",
                       help="default on_insert skips empty batches; every_round explicitly forces a rebuild each round")
    p.add_argument("--shift",type=int,default=0,
                   help="0 selects ell from max-delta; value ring is Z_(2^(64-ell)), not fixed-point")
    p.add_argument("--seed",type=int,default=42)
    p.add_argument("--repeat",type=int,default=3)
    p.add_argument("--warmup",type=int,default=0)
    p.add_argument("--threads",type=int,default=1)
    p.add_argument("--port",type=int,default=22500)
    p.add_argument("--timeout",type=float,default=300,help="seconds per trial")
    p.add_argument("--check",action="store_true",help="verify every iteration outside timing")
    p.add_argument("--out",type=Path,help="new output directory")
    a=p.parse_args()
    a.backend=a.init_backend="two_party";a.parties=2
    a.graphiti_mode="rebuild" if rebuild else "static"
    if not rebuild:
        a.insert_file=a.insert_schedule=None;a.insert_per_round=0;a.reinit_policy=None
    if a.rounds is not None and a.rounds<1:p.error("--rounds must be positive")
    if a.vertices+a.base_edges>1<<22:p.error("V+E must be <= 2^22")
    return a


def main(rebuild=False):
    a=options(rebuild)
    if not a.binary.is_file():raise ValueError(f"build bench_FullGraphiti first: {a.binary}")
    initial,base,rounds,cap,ell,q=common.workload(a)
    a.rounds=len(rounds)
    if a.vertices+a.base_edges+sum(map(len,rounds))>1<<22:
        raise ValueError("V+E including all insertions must be <= 2^22")
    benchmark="graphiti_rebuild" if rebuild else "graphiti_full"
    out=a.out or Path("Results")/benchmark/dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    out.mkdir(parents=True,exist_ok=False)
    meta=dict(schema_version=2,benchmark=benchmark,backend="two_party",num_processes=2,
              init_backend="two_party",initialization="shared_endpoints",vertices=a.vertices,
              base_edges=a.base_edges,rounds=a.rounds,insert_schedule=list(map(len,rounds)),seed=a.seed,
              reinit_policy=a.reinit_policy,state_policy="continue",reuse_ot_contexts=True,
              application=a.application,max_delta=cap,shift=ell,q=q,check=a.check,
              correctness_checked=False,threads=a.threads,warmup=a.warmup,repeat=a.repeat,
              platform=platform.platform(),binary=str(a.binary.resolve()),
              workload_sha256=hashlib.sha256(json.dumps([initial,base,rounds]).encode()).hexdigest())
    (out/"metadata.json").write_text(json.dumps(meta,indent=2))
    samples=[];parties=[]
    plan=[("init",-1,0,0)];active=0
    for t,row in enumerate(rounds):
        active+=len(row)
        if rebuild:plan.append(("reinit",t,len(row),active))
        plan.append(("iter",t,0,active))
    with tempfile.TemporaryDirectory(prefix="cris-graphiti-full-") as tmp:
        d=Path(tmp);common.prepare(a,d,initial,base,rounds,cap,q)
        for party in range(2):
            path=d/f"in-{party}.json";content=json.loads(path.read_text())
            content.update(graphiti_mode=a.graphiti_mode,reinit_policy=a.reinit_policy or "on_insert")
            path.write_text(json.dumps(content,separators=(",",":")))
        for rep in range(-a.warmup,a.repeat):
            logs=out/f"trial-{rep}";logs.mkdir()
            data,raw=common.trial(a,d,rounds,rep,logs,plan=plan)
            for i in range(0,len(raw),2):
                for sender,receiver in ((raw[i],raw[i+1]),(raw[i+1],raw[i])):
                    if sender["two_party_tx"]!=receiver["two_party_rx"]:
                        raise RuntimeError("two-party sent/received payload mismatch")
            if a.check:common.verify(a,d,initial,base,rounds,q)
            if rep>=0:
                if rebuild:
                    for row in data:
                        row["current_edges"]=a.base_edges+row["active_delta"]
                        row["reinitialized"]=(row["phase"]=="reinit" and
                            (a.reinit_policy=="every_round" or row["inserted"]>0))
                samples.extend(data);parties.extend(raw)
                common.csv_write(out/"samples.csv",samples)
                (out/"parties.jsonl").write_text("".join(json.dumps(r)+"\n" for r in parties))
    keys=("ms","nph_bytes","nph_helper_bytes","risa_bytes","two_party_bytes","total_bytes")
    summary=[]
    def add(phase,t,new,active,group):
        row=dict(phase=phase,round=t,inserted=new,active_delta=active,
                 **{"median_"+key:statistics.median(r[key] for r in group) for key in keys})
        if rebuild:
            row["current_edges"]=a.base_edges+active
            row["reinitialized"]=(phase=="reinit" and (a.reinit_policy=="every_round" or new>0))
        summary.append(row)
    for phase,t,new,active in plan:
        add(phase,t,new,active,[r for r in samples if r["phase"]==phase and r["round"]==t])
    totals=[{key:sum(r[key] for r in samples if r["repeat"]==rep) for key in keys}
            for rep in range(a.repeat)]
    add("total",-1,active,active,totals)
    common.csv_write(out/"summary.csv",summary)
    meta["correctness_checked"]=a.check
    (out/"metadata.json").write_text(json.dumps(meta,indent=2))
    print("phase    t    new active    median_ms     TWO_PC_bytes    total_bytes")
    for r in summary:
        print(f'{r["phase"]:6} {r["round"]:3} {r["inserted"]:5} {r["active_delta"]:6} {r["median_ms"]:12.3f}'
              f' {r["median_two_party_bytes"]:16.0f} {r["median_total_bytes"]:14.0f}')
    print(f"Results: {out}"+(" (every iteration verified)" if a.check else ""))


if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        sys.exit(f"benchmark: {exc}")
