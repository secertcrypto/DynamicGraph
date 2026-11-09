#!/usr/bin/env python3
"""Launch exactly two RISA compute parties; no preprocessing helper."""
import argparse
import pathlib
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=pathlib.Path)
parser.add_argument("--port", type=int, default=28465)
parser.add_argument("--timeout", type=float, default=100)
args = parser.parse_args()
if not 1024 <= args.port <= 65535:
    parser.error("port must be in [1024,65535]")
processes, streams = [], []
with tempfile.TemporaryDirectory(prefix="risa-test-") as folder:
    paths = [pathlib.Path(folder) / f"party-{i}.log" for i in range(2)]
    try:
        for i, path in enumerate(paths):
            stream = path.open("w")
            streams.append(stream)
            processes.append(subprocess.Popen(
                [str(args.binary.resolve()), str(i), str(args.port)],
                stdout=stream, stderr=subprocess.STDOUT))
        deadline = time.monotonic() + args.timeout
        while any(p.poll() is None for p in processes):
            if any(p.poll() not in (None, 0) for p in processes):
                raise RuntimeError("a party failed; terminating its peer")
            if time.monotonic() > deadline:
                raise TimeoutError("two-party execution timed out")
            time.sleep(0.05)
        if any(p.returncode != 0 for p in processes):
            raise RuntimeError("a party failed")
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill()
            p.wait()
        for stream in streams:
            stream.close()
        for path in paths:
            if path.exists():
                print(path.read_text(), end="")
