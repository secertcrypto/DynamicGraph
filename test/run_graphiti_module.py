"""Launch the independent module example and propagate every party's failures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    binary, backend, port, *mode = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="graphiti-module-") as directory:
        processes, logs = [], []
        try:
            for pid in range(2 if backend=="two_party" else 3):
                log = open(Path(directory) / f"party-{pid}.log", "w+")
                logs.append(log)
                env = dict(os.environ, OMP_NUM_THREADS="1")
                processes.append(subprocess.Popen(
                    [binary, str(pid), backend, port, *mode],
                    stdout=log, stderr=subprocess.STDOUT, env=env))
            deadline = time.monotonic() + 45
            for process in processes:
                process.wait(timeout=max(0.1, deadline - time.monotonic()))
            if any(p.returncode != 0 for p in processes):
                raise RuntimeError("at least one party failed")
            for pid, log in enumerate(logs):
                log.seek(0)
                output = log.read()
                if f"Graphiti module PASS P{pid}" not in output:
                    raise RuntimeError(f"missing success result for P{pid}")
            print(f"Graphiti module PASS ({backend}, {mode or ['optimized']})")
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.wait()
            for pid, log in enumerate(logs):
                log.seek(0)
                print(f"P{pid}:\n{log.read()}")
                log.close()


if __name__ == "__main__":
    main()
