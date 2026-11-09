"""Check real initialization, per-round correctness, ring boundaries and counters."""
import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--dynamic-binary",type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    launcher=root/"benchmark/graphiti/run_full.py"
    cases=[(7,11,4,"weighted_sum",9,6,2,1),
           (5,9,5,"reachability",9,6,1,0),
           (1,0,3,"weighted_sum",0,1,1,0),
           (4,0,2,"reachability",0,63,1,0)]
    with tempfile.TemporaryDirectory(prefix="graphiti-full-test-") as tmp:
        folder=Path(tmp)
        for index,(v,e,k,app,cap,shift,repeat,warmup) in enumerate(cases):
            out=folder/str(index)
            command=[sys.executable,str(launcher),"--binary",str(args.binary),
                     "--vertices",str(v),"--edges",str(e),"--rounds",str(k),
                     "--application",app,"--max-delta",str(cap),"--shift",str(shift),
                     "--repeat",str(repeat),"--warmup",str(warmup),"--port",str(22500+index),
                     "--timeout","60","--check","--out",str(out)]
            subprocess.run(command,check=True,timeout=120)
            meta=json.loads((out/"metadata.json").read_text())
            assert meta["initialization"]=="shared_endpoints" and meta["correctness_checked"]
            assert meta["q"]==1<<(64-shift) and meta["num_processes"]==2
            with (out/"samples.csv").open() as f:samples=list(csv.DictReader(f))
            with (out/"summary.csv").open() as f:summary=list(csv.DictReader(f))
            assert len(samples)==repeat*(k+1) and len(summary)==k+2
            assert [r["phase"] for r in summary]==["init"]+["iter"]*k+["total"]
            for row in samples:
                assert float(row["ms"])>=0 and math.isfinite(float(row["ms"]))
                assert int(row["risa_bytes"])==int(row["nph_bytes"])==0
                assert int(row["total_bytes"])==int(row["two_party_bytes"])>0
            raw=[json.loads(line) for line in (out/"parties.jsonl").read_text().splitlines()]
            assert len(raw)==2*len(samples)
            for sample in samples:
                peers=[r for r in raw if r["repeat"]==int(sample["repeat"])
                       and r["phase"]==sample["phase"] and r["round"]==int(sample["round"])]
                assert sum(r["two_party_tx"] for r in peers)==int(sample["total_bytes"])
            for key in ("ms","total_bytes"):
                totals=[sum(float(r[key]) for r in samples if int(r["repeat"])==rep)
                        for rep in range(repeat)]
                assert math.isclose(float(summary[-1]["median_"+key]),sum(totals)/len(totals))
            if index==0 and args.dynamic_binary:
                dyn=folder/"dynamic"
                subprocess.run([sys.executable,str(root/"benchmark/dynamic_graph/run.py"),
                    "--binary",str(args.dynamic_binary),"--backend","two_party",
                    "--vertices",str(v),"--base-edges",str(e),"--rounds",str(k),
                    "--insert-per-round","0","--application",app,"--max-delta",str(cap),
                    "--shift",str(shift),"--repeat","1","--port","22600",
                    "--check","--out",str(dyn)],check=True,timeout=90)
                dynamic_meta=json.loads((dyn/"metadata.json").read_text())
                assert dynamic_meta["workload_sha256"]==meta["workload_sha256"]
                assert dynamic_meta["q"]==meta["q"]
        bad=subprocess.run([sys.executable,str(launcher),"--binary",str(args.binary),
                            "--rounds","0"],capture_output=True,text=True)
        assert bad.returncode!=0 and "--rounds must be positive" in bad.stderr
    print("PASS full Graphiti initialization, iterations, fixtures and accounting")


if __name__=="__main__":main()
