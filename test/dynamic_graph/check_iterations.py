"""Public fixtures only: compare both local outputs to a separate cleartext graph."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import time

CASES = [
    # Parallel and self edges, zero and negative weights, isolated vertex 4.
    (5, 3, 0, [-1,3,1,4,0],
     [(3,0,-1),(1,1,5),(0,2,7),(0,2,3),(2,3,0)],
     [[],[(90,3,1,2),(10,0,1,4)],[],[(30,1,4,-3)],[]]),
    # q=16: repeated updates wrap in the *value* ring, no base edges.
    (3, 2, 60, [14,7,15], [],
     [[(2,0,0,-1)],[(3,2,1,7)],[]]),
    (1, 0, 0, [19], [], [[],[]]),
    # BFS: the inserted 2->3 edge must see the old snapshot; 3 is reached
    # on the NEXT round after 2 is reached. Parallel edges produce count 2.
    (4, 2, 0, [1,0,0,0], [(1,2,9),(0,1,0),(0,1,-1)],
     [[],[(8,2,3,0)],[]], "reachability"),
]

def make(case, directory, two_party_init=False, backend="nph"):
    parties=2 if backend=="two_party" else 3
    n, cap, shift, h, edges, rounds, *app = case
    name = app[0] if app else "weighted_sum"
    ell = shift or max(1, (2*cap).bit_length())
    q = 1 << (64-ell)
    rng = random.Random(3841)
    def split(x, mod):
        a = rng.randrange(mod)
        return a, (x-a) % mod
    inputs = [dict(backend=backend,two_party_init=two_party_init,application=name, vertices=n, max_delta=cap, shift=shift, base_edges=len(edges),
                   rounds=[]) for _ in range(parties)]
    for p in (0,1):
        inputs[p]["base_rows"] = []
    for src,dst,data in [(v,v,h[v]) for v in range(n)] + edges:
        shares = [split(src,n),split(dst,n),split(data,q)]
        for p in (0,1):
            inputs[p]["base_rows"].append([s[p] for s in shares])
    active = list(edges)
    h = [v % q for v in h]
    reference = []
    for additions in rounds:
        records = [[] for _ in range(parties)]
        for eid,src,dst,w in additions:
            shares = [split(src,n),split(dst,n),split(w,q)]
            for p in (0,1):
                records[p].append(dict(id=eid, src=shares[0][p], dst=shares[1][p],
                                       data=shares[2][p]))
            if parties==3: records[2].append(dict(id=eid))
            active.append((src,dst,w))
        for p in range(parties):
            inputs[p]["rounds"].append(dict(insert=records[p]))
        next_h = h.copy()
        if name=="weighted_sum":
            for src,dst,w in active: next_h[dst] += h[src] * w
        else:
            for src,dst,w in active:
                if h[src]: next_h[dst] = 1
        h = [v % q for v in next_h]
        reference.append((h,len(active)-len(edges)))
    for p in range(parties):
        (directory/f"party-{p}.json").write_text(json.dumps(inputs[p],indent=2))
    return q, reference

def run(binary, port, directory, q, ref, parties=3):
    procs=[]; logs=[]
    try:
        for p in range(parties):
            log=(directory/f"p{p}.log").open("w")
            logs.append(log)
            procs.append(subprocess.Popen([binary,str(p),str(port),
                directory/f"party-{p}.json",directory/f"shares-{p}.jsonl"],
                stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,OMP_NUM_THREADS="1")))
        deadline=time.monotonic()+65
        while any(p.poll() is None for p in procs):
            if any(p.poll() not in (0,None) for p in procs) or time.monotonic()>deadline:
                raise RuntimeError("runtime failed or timed out")
            time.sleep(.05)
        if any(p.returncode for p in procs): raise RuntimeError("party failed")
        outputs=[[json.loads(line) for line in (directory/f"shares-{p}.jsonl").read_text().splitlines()] for p in range(parties)]
        if any(len(rows)!=len(ref) for rows in outputs): raise ValueError("round count mismatch")
        for t,(expected,active) in enumerate(ref):
            rows=[out[t] for out in outputs]
            if any(r["active_edges"]!=active or r["iteration"]!=t+1 for r in rows):
                raise ValueError("iteration metadata mismatch")
            if (parties==3 and rows[2]["vertex_shares"]) or any(len(rows[p]["vertex_shares"])!=len(expected) for p in (0,1)):
                raise ValueError("wrong share sizes")
            result=[(a+b)%q for a,b in zip(rows[0]["vertex_shares"],rows[1]["vertex_shares"])]
            if result!=expected: raise ValueError(f"round {t}: {result} != {expected}")
            print(f"t={t} edges={active} EXACT {result}")
    finally:
        for p in procs:
            if p.poll() is None: p.kill()
            p.wait()
        for f in logs: f.close()
        for p in range(len(procs)):
            print((directory/f"p{p}.log").read_text(),end="")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("binary",nargs="?",type=Path)
    parser.add_argument("--backend",choices=["nph","two_party"],default="nph")
    parser.add_argument("--two-party-init",action="store_true")
    parser.add_argument("--port",type=int,default=19500)
    parser.add_argument("--export",type=Path,help="write the public demo's three local input files")
    args=parser.parse_args()
    if args.export:
        args.export.mkdir(parents=True,exist_ok=False)
        make(CASES[0],args.export,args.two_party_init,args.backend)
    if args.binary:
        for case_idx,case in enumerate(CASES):
            with tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp)
                q,ref=make(case,d,args.two_party_init,args.backend)
                run(str(args.binary.resolve()),args.port+case_idx*10,d,q,ref,2 if args.backend=="two_party" else 3)
    if not args.export and not args.binary: parser.error("supply a binary or --export DIR")
