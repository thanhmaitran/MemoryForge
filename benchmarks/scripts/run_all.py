#!/usr/bin/env python3
"""Run every MemoryForge workload config and store CSV + JSON results.

Usage:
    python benchmarks/scripts/run_all.py --exe build/release/memoryforge --out reports/local
"""
import argparse
import subprocess
import sys
from pathlib import Path


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd]).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, help="path to the memoryforge executable (Release build)")
    parser.add_argument("--configs", default="configs", help="configuration directory")
    parser.add_argument("--out", default="reports/local", help="output directory")
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()

    configs, out = Path(args.configs), Path(args.out)
    status = 0

    for cfg in sorted((configs / "allocator").glob("*.json")):
        base = out / "allocator" / cfg.stem
        status |= run([args.exe, "allocator", "run", "--config", cfg, "--repetitions", args.repetitions,
                       "--output", f"{base}.summary.csv", "--timeseries", f"{base}.timeseries.csv",
                       "--json", f"{base}.json"])

    for cfg in sorted((configs / "virtual-memory").glob("*.json")):
        base = out / "virtual-memory" / cfg.stem
        status |= run([args.exe, "vm", "run", "--config", cfg, "--repetitions", args.repetitions,
                       "--output", f"{base}.summary.csv", "--json", f"{base}.json"])

    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
