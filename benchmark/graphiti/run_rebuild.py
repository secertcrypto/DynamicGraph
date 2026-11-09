#!/usr/bin/env python3
"""Launch Graphiti with insertions, complete topology reinitialization and iteration."""
import subprocess
import sys
from run_full import main

if __name__=="__main__":
    try:main(rebuild=True)
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        sys.exit(f"benchmark: {exc}")
