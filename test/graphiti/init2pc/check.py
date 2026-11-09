#!/usr/bin/env python3
"""Exactly two processes; opens ONLY public test fixtures outside the protocol."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import time

MASK=(1<<64)-1
CASES=[
    (1,[]),
    (2,[]),
    (3,[]),
    (4,[(0,1),(0,1),(1,1),(3,0),(0,3)]),
    (5,[(0,4),(1,4),(4,4),(3,1),(2,0),(2,0),(0,2),(4,3)]),
    (8,[(0,7),(7,0),(1,2),(1,2),(2,2),(5,6),(5,0),(4,3),(4,7)]),
]

def run_case(binary,port,vertices,edges,mode,rounds=3):
    ends=[(i,i) for i in range(vertices)]+edges
    n=len(ends);rng=random.Random(39183+n)
    def split(x,mod):
        r=rng.randrange(mod)
        return r,(x-r)%mod
    inputs=[dict(vertices=vertices,index_sharing=mode,src=[],dst=[],payloads=[]) for _ in range(2)]
    for src,dst in ends:
        for name,x in (("src",src),("dst",dst)):
            s=split(x,vertices if mode=="mod_vertices" else 1<<64)
            for p in range(2):inputs[p][name].append(s[p])
    truth=[]
    for t in range(rounds):
        # ID is an extra shared payload; not exposed by Init. Vary all data each reuse.
        data=[[src,dst,int(i<vertices),rng.getrandbits(64),i]
              for i,(src,dst) in enumerate(ends)]
        truth.append(data)
        batches=[[],[]]
        for row in data:
            shares=[split(x,1<<64) for x in row]
            for p in range(2):batches[p].append([s[p] for s in shares])
        for p in range(2):inputs[p]["payloads"].append(batches[p])
    with tempfile.TemporaryDirectory(prefix="init2pc-") as tmp:
        d=Path(tmp);ps=[];logs=[]
        try:
            for p in range(2):
                (d/f"in-{p}.json").write_text(json.dumps(inputs[p]))
                log=(d/f"p{p}.log").open("w");logs.append(log)
                ps.append(subprocess.Popen([str(binary),str(p),str(port),str(d/f"in-{p}.json"),str(d/f"out-{p}.json")],
                    stdout=log,stderr=subprocess.STDOUT))
            deadline=time.monotonic()+90
            while any(p.poll() is None for p in ps):
                if any(p.poll() not in (None,0) for p in ps):raise RuntimeError("worker failed")
                if time.monotonic()>deadline:raise TimeoutError("two-party init timed out")
                time.sleep(.02)
            if any(p.returncode for p in ps):raise RuntimeError("worker failed")
            out=[json.loads((d/f"out-{p}.json").read_text()) for p in range(2)]
        except Exception:
            for p in range(2):
                f=d/f"p{p}.log"
                if f.exists():print(f.read_text(),flush=True)
            raise
        finally:
            for p in ps:
                if p.poll() is None:p.kill()
                p.wait()
            for log in logs:log.close()
    for field in ("source_mapping","destination_mapping"):
        if out[0][field]!=out[1][field] or sorted(out[0][field])!=list(range(n)):
            raise AssertionError("public mapping is not an agreed permutation")
    c=[(a+b)&MASK for a,b in zip(out[0]["return_mapping_share"],out[1]["return_mapping_share"])]
    if sorted(c)!=list(range(n)):raise AssertionError("C shares do not reconstruct a permutation")
    source_ids=sorted(range(n),key=lambda i:(ends[i][0],int(i>=vertices),i))
    dest_ids=sorted(range(n),key=lambda i:(ends[i][1],int(i<vertices),i))
    def push(order):
        result=[None]*n
        for position,original in enumerate(order):result[original]=position
        return result
    vs,vd=push(source_ids),push(dest_ids)
    expected_orders=[vs,[vd[i] for i in source_ids],dest_ids]
    for i in range(3):
        got=[(a+b)&MASK for a,b in zip(out[0]["order_shares"][i],out[1]["order_shares"][i])]
        if got!=expected_orders[i]:raise AssertionError("Graphiti push-label export mismatch")
    for t in range(rounds):
        opened={}
        for field,ids in (("source",source_ids),("destination",dest_ids),("vertex",list(range(n)))):
            a,b=[r["rounds"][t][field] for r in out]
            got=[[(x+y)&MASK for x,y in zip(ra,rb)] for ra,rb in zip(a,b)]
            if got!=[truth[t][i] for i in ids]:raise AssertionError(f"{field}, round {t}: mismatched records")
            opened[field]=got
        b_order=[None]*n
        for row,pos in zip(opened["destination"],out[0]["destination_mapping"]):b_order[pos]=row[4]
        if [b_order[i] for i in c]!=list(range(n)):raise AssertionError("wrong composite inverse C")
        if t and n>1 and out[0]["rounds"][t]["vertex"]==out[0]["rounds"][t-1]["vertex"]:
            raise AssertionError("unexpected unchanged shares")
    print(f"PASS init2pc V={vertices} E={len(edges)} sharing={mode} reuse={rounds} "
          f"init_ms={max(r['init_ms'] for r in out):.3f} sent_bytes={sum(r['init_sent_bytes'] for r in out)}",flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary",type=Path,required=True)
    p.add_argument("--port",type=int,default=29800)
    a=p.parse_args()
    for i,(v,e) in enumerate(CASES):
        run_case(a.binary.resolve(),a.port,v,e,"mod_vertices")
    run_case(a.binary.resolve(),a.port,5,CASES[4][1],"ring64")
    print("PASS: two-party source/destination order, inverse mapping and repeated round trips")
if __name__=="__main__":main()
