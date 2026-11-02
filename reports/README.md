# Reports

This directory holds one **reference run** of every shipped workload (`allocator/`, `virtual-memory/`), produced with

```text
python benchmarks/scripts/run_all.py --exe <memoryforge> --out reports --repetitions 5
python analysis/plot_results.py --reports reports --out docs/images/results
```

## Provenance of the committed run

| | |
| --- | --- |
| Compiler / build | GCC 13.3.0, Release (`-O2 -DNDEBUG`), built without CMake by a direct `g++` invocation |
| OS / CPU | Linux, Intel Xeon @ 2.10 GHz, 1 hardware thread (a shared sandbox VM) |
| `git_commit` | `unknown` (the sources were not in a git checkout) |
| Repetitions | 5 per strategy / policy |

**What can be trusted.** All *algorithmic* columns (failure counts, search steps, fragmentation, page faults,
write-backs) are deterministic functions of seed + trace + algorithm and reproduce exactly on any machine; running
`memoryforge validate --all` re-checks the invariants behind them.

**What must not be trusted.** The *wall-clock* columns (latency, throughput) come from a single shared virtual CPU and
were not taken under controlled conditions. Regenerate them on your own machine with `scripts\benchmark.ps1` before
quoting any timing number, and replace this run if you publish results.

Files per workload: `<name>.summary.csv` (one row per strategy/policy), `<name>.timeseries.csv` (fragmentation over
time, allocator only), `<name>.json` (full metadata and per-repetition values).
