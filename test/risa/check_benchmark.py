"""Exercise standalone RISA phases, reusable batches, actual reads and counters."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


def rows(path):
    with path.open() as f:return list(csv.DictReader(f))


def main():
    p=argparse.ArgumentParser();p.add_argument("--binary",type=Path,required=True);args=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    launcher=root/"benchmark/primitives/run_risa.py"
    cases=[(5,["--insert-schedule","0,2,1,0","--read-batch","2","--repeat","2","--warmup","1"]),
           (1,["--batch-size","2","--rounds","3","--repeat","1"]),
           (4,["--batch-size","0","--rounds","2","--repeat","1"]),
           (2053,["--batch-size","1","--rounds","2","--update-batch","0","--repeat","1"]),
           (3,["--insert-per-round","1","--rounds","2","--update-batch","1","--read-batch","0","--repeat","1"])]
    with tempfile.TemporaryDirectory(prefix="risa-bench-test-") as tmp:
        for i,(n,options) in enumerate(cases):
            out=Path(tmp)/str(i)
            subprocess.run([sys.executable,str(launcher),"--binary",str(args.binary),"--database-size",str(n),
                *options,"--port","23200","--timeout","90","--check","--out",str(out)],check=True,timeout=180)
            meta=json.loads((out/"metadata.json").read_text())
            assert meta["correctness_checked"] and meta["database_size"]==n
            samples=rows(out/"samples.csv");summary=rows(out/"summary.csv")
            per_trial=3+5*meta["rounds"]
            assert len(samples)==per_trial*meta["repeat"] and len(summary)==per_trial+1
            assert [r["phase"] for r in summary]==["connect","setup_update","setup_read"]+[
                "compile_read","compile_update","update","apply_update","read"]*meta["rounds"]+["total"]
            for row in samples:
                total=int(row["total_bytes"]);phase=row["phase"];batch=int(row["batch_size"])
                assert int(row["p0_tx"])+int(row["p1_tx"])==total
                assert float(row["ms"])>=0 and math.isfinite(float(row["ms"]))
                if phase=="update":assert total==16*batch
                if phase=="read":assert total==2*batch*meta["read_response_bytes"]
                if phase in ("connect","setup_update","apply_update"):assert total==0
                if phase.startswith("compile_") and batch==0:assert total==0
                if phase=="setup_read":assert total>0
            for key in ("ms","p0_tx","p1_tx","total_bytes"):
                values=[sum(float(r[key]) for r in samples if int(r["repeat"])==rep) for rep in range(meta["repeat"])]
                assert math.isclose(float(summary[-1]["median_"+key]),statistics.median(values))
            for subtotal in rows(out/"phase_totals.csv"):
                values=[sum(int(r["total_bytes"]) for r in samples if int(r["repeat"])==rep and r["phase"]==subtotal["phase"])
                        for rep in range(meta["repeat"])]
                assert float(subtotal["median_total_bytes"])==statistics.median(values)
        bad=subprocess.run([sys.executable,str(launcher),"--binary",str(args.binary),"--database-size","0"],capture_output=True)
        assert bad.returncode!=0
    print("PASS RISA phase benchmark: updates, post-update reads, empty batches, bounds and accounting")


if __name__=="__main__":main()
