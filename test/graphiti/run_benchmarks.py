"""Exercise the Graphiti CLI, distributed results, and phase accounting."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run_case(binary, options, args, port, check=True):
    with tempfile.TemporaryDirectory(prefix="graphiti-benchmark-test-") as directory:
        folder = Path(directory)
        processes, logs = [], []
        count = args.parties + (args.protocol == "nph")
        try:
            for pid in range(count):
                log = (folder / f"party-{pid}.log").open("w+")
                logs.append(log)
                command = [str(binary), "--pid", str(pid), "--protocol", args.protocol,
                           "--num-parties", str(args.parties), "--port", str(port),
                           "--output", str(folder / f"party-{pid}.json"), *options]
                if args.generic:
                    command.append("--disable-optimized-shuffle")
                if args.pking:
                    command.append("--pking")
                processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                                  env=dict(os.environ, OMP_NUM_THREADS="1")))
            deadline = time.monotonic() + 60
            while any(p.poll() is None for p in processes):
                if any(p.poll() not in (None, 0) for p in processes):
                    raise RuntimeError("a benchmark party failed")
                if time.monotonic() >= deadline:
                    raise TimeoutError("benchmark parties timed out")
                time.sleep(0.02)
            if any(p.returncode != 0 for p in processes):
                raise RuntimeError("a benchmark party failed")
            results=[]
            for pid in range(count):
                result = json.loads((folder / f"party-{pid}.json").read_text())
                results.append(result)
                helper = args.protocol == "nph" and pid == args.parties
                if binary == args.init and "model" in result:
                    # Legacy synthetic benchmark: validate its cost-model schema,
                    # never treat it as a correctness test of initialization.
                    assert result["details"]["benchmark"] == "microbench_graphiti_init"
                    assert result["model"]["secure_shuffles"] == 3
                    assert result["circuit"]["ltz_gates"] == result["model"]["comparisons_total"]
                    for phase in ("offline","online","total"):
                        assert result[phase]["time_ms"] >= 0
                    continue
                assert result["correctness_checked"] == (check and not helper), result
                assert result["correct"] is (True if check and not helper else None), result
                assert result["details"]["benchmark"] == binary.name
                assert result["details"]["schema_version"] == 2
                assert result["details"]["is_helper"] == helper
                for phase in ("offline", "init", "online", "total", "reconstruction"):
                    stats = result[phase]
                    assert math.isfinite(stats["time_ms"]) and stats["time_ms"] >= 0
                    for direction in ("sent", "recv"):
                        assert len(stats[f"bytes_{direction}"]) == count
                        assert sum(stats[f"bytes_{direction}"]) == stats[f"total_bytes_{direction}"]
                for field in ("time_ms", "total_bytes_sent", "total_bytes_recv"):
                    expected = sum(result[phase][field] for phase in ("offline", "init", "online"))
                    assert math.isclose(result["total"][field], expected, rel_tol=1e-9)
                if binary == args.init:
                    assert result["details"]["synthetic_sort_model"] is False
                    assert result["details"]["online_scope"] in ("initialization", "initialization_and_order_export")
            if args.protocol=="two_party":
                for phase in ("offline","init","online","reconstruction"):
                    if phase in results[0]:
                        for pid in range(2):
                            assert results[pid][phase]["bytes_sent"][1-pid] == results[1-pid][phase]["bytes_recv"][pid]
            print(f"PASS {binary.name} {' '.join(options)}", flush=True)
        except Exception:
            for log in logs:
                log.flush()
                log.seek(0)
                print(log.read(), flush=True)
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.wait()
            for log in logs:
                log.close()


def main():
    parser = argparse.ArgumentParser()
    for name in ("bfs", "pagerank", "init", "shuffle"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--protocol", choices=("rss3", "nph", "two_party"), required=True)
    parser.add_argument("--parties", type=int, required=True)
    parser.add_argument("--port", type=int, default=23500)
    parser.add_argument("--generic", action="store_true")
    parser.add_argument("--pking", action="store_true")
    args = parser.parse_args()
    graph = ["--num-verts", "5", "--num-edges", "9", "--seed", "7"]
    isolated = ["--num-verts", "1", "--num-edges", "0"]
    cases = [
        (args.bfs, [*graph, "--num-hops", "4", "--source-vertex", "3"], True),
        (args.bfs, [*graph, "--num-hops", "4", "--skip-final-applyv"], True),
        (args.bfs, [*isolated, "--num-hops", "2"], True),
        (args.bfs, ["--num-verts", "2", "--num-edges", "100", "--num-hops", "80"], True),
        (args.pagerank, [*graph, "--num-iters", "3"], True),
        (args.pagerank, [*isolated, "--num-iters", "2"], True),
        (args.init, graph, True),
        (args.init, isolated, True),
        (args.shuffle, ["--vec-size", "7", "--num-repeats", "3"], True),
        (args.shuffle, ["--vec-size", "1", "--num-repeats", "1"], True),
        (args.pagerank, ["--graph-size", "20", "--num-iters", "1", "--no-check"], False),
    ]
    for i, (binary, options, check) in enumerate(cases):
        run_case(binary, options, args, args.port + 20 * i, check)
    for invalid in (
        ["--num-verts", "-1", "--num-edges", "2"],
        ["--graph-size", "20", "--num-verts", "2", "--num-edges", "18"],
        ["--num-verts", "1", "--num-edges", "1"],
        ["--graph-size", "20", "--num-hops", "0"],
        ["--graph-size", "20", "--source-vertex", "2"],
        ["--graph-size", "20", "--port", "65535"],
        ["--graph-size", "20x"],
    ):
        result = subprocess.run([str(args.bfs), "--pid", "0", *invalid], capture_output=True, timeout=5)
        assert result.returncode != 0, invalid


if __name__ == "__main__":
    main()
