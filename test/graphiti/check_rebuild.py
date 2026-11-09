"""Check Graphiti rebuilding against the dynamic workload and per-round oracle."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


def read_csv(path):
    with path.open() as f:return list(csv.DictReader(f))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--dynamic-binary",type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    launcher=root/"benchmark/graphiti/run_rebuild.py"
    with tempfile.TemporaryDirectory(prefix="graphiti-rebuild-test-") as tmp:
        folder=Path(tmp)
        inserts=folder/"insert.json"
        # Empty rounds, self-loop, parallel destinations and signed edge data.
        inserts.write_text(json.dumps([[],[[10,0,0,-2],[11,0,2,3]],[],[[12,0,2,-1]]]))
        for app in ("weighted_sum","reachability"):
            fingerprints=[]
            shared=["--vertices","5","--base-edges","3","--insert-file",str(inserts),
                    "--max-delta","3","--shift","4","--application",app,"--seed","42","--check"]
            for policy in ("every_round","on_insert"):
                out=folder/f"{app}-{policy}"
                subprocess.run([sys.executable,str(launcher),"--binary",str(args.binary),
                    *shared,"--repeat","2","--warmup","1","--reinit-policy",policy,
                    "--port","22800","--out",str(out)],check=True,timeout=120)
                meta=json.loads((out/"metadata.json").read_text())
                assert meta["correctness_checked"] and meta["reinit_policy"]==policy
                assert meta["insert_schedule"]==[0,2,0,1] and meta["state_policy"]=="continue"
                assert meta["reuse_ot_contexts"] and meta["rounds"]==4
                fingerprints.append((meta["workload_sha256"],meta["q"]))
                samples=read_csv(out/"samples.csv");summary=read_csv(out/"summary.csv")
                assert len(samples)==18 and len(summary)==10
                assert [r["phase"] for r in summary]==["init"]+["reinit","iter"]*4+["total"]
                for row in samples:
                    t=int(row["round"]);active=0 if t<0 else [0,2,2,3][t]
                    assert int(row["active_delta"])==active and int(row["current_edges"])==3+active
                    assert int(row["nph_bytes"])==int(row["risa_bytes"])==0
                    assert int(row["total_bytes"])==int(row["two_party_bytes"])
                    if row["phase"]=="reinit":
                        changed=policy=="every_round" or t in (1,3)
                        assert (row["reinitialized"]=="True")==changed
                        assert (int(row["total_bytes"])>0)==changed
                for key in ("ms","total_bytes"):
                    totals=[sum(float(r[key]) for r in samples if int(r["repeat"])==rep) for rep in range(2)]
                    assert abs(float(summary[-1]["median_"+key])-statistics.median(totals))<1e-6
                if policy=="every_round":
                    # Same topology, but base OT setup is reused after the initial Init.
                    assert float(summary[1]["median_total_bytes"])<float(summary[0]["median_total_bytes"])
            if args.dynamic_binary:
                out=folder/f"dynamic-{app}"
                subprocess.run([sys.executable,str(root/"benchmark/dynamic_graph/run.py"),
                    "--binary",str(args.dynamic_binary),"--backend","two_party",*shared,
                    "--repeat","1","--port","22900","--out",str(out)],check=True,timeout=90)
                meta=json.loads((out/"metadata.json").read_text())
                fingerprints.append((meta["workload_sha256"],meta["q"]))
            assert len(set(fingerprints))==1
        # Reject an insufficient insertion bound before launching parties.
        bad=subprocess.run([sys.executable,str(launcher),"--binary",str(args.binary),
            "--vertices","3","--edges","2","--rounds","2",
            "--insert-per-round","1","--max-delta","1"],capture_output=True,text=True,timeout=15)
        assert bad.returncode!=0 and "max-delta must cover all insertions" in bad.stderr
    print("PASS Graphiti rebuild, state continuation, insertion policies and dynamic alignment")


if __name__=="__main__":main()
