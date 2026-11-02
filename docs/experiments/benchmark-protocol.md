# Benchmark protocol

## Principles

1. **Seeded workloads.** Every workload is a JSON config with a seed. The same config yields a byte-identical trace on
   every platform (own PRNG, no `<random>` distributions). The trace checksum is recorded in every report.
2. **Same input for every strategy.** A trace refers to allocations by ordinal, not by allocator-issued id, so the
   same trace can be replayed against First, Best and Worst Fit even when some allocations fail.
3. **Two passes per run.**
   * *Deterministic pass* (untimed): replays the whole trace once, collects every algorithmic metric, samples the
     fragmentation time series, optionally runs invariant checks, and finally frees every live allocation and checks
     that the arena is one free block again (no logical leak, full coalescing).
   * *Timed repetitions*: for each repetition a fresh allocator replays the trace twice - once with only two clock
     reads around the measured region (throughput) and once timing every call (latency). The first
     `warmup_operations` operations run but are excluded from both measurements.
4. **Release builds only.** Reports record the build type; Debug results must never be compared with Release results.
   The CLI prints a warning for non-optimised builds.

## Procedure

```text
# Windows (Visual Studio Build Tools)
.\scripts\benchmark.ps1 -Repetitions 7

# Linux / macOS
cmake --preset release && cmake --build --preset release
python benchmarks/scripts/run_all.py --exe build/release/memoryforge --out reports/local --repetitions 7
python analysis/plot_results.py --reports reports/local
```

Results land in `reports/local/` (git-ignored). Copy a run into `reports/` only after reviewing it, together with its
JSON metadata.

## Controlling noise

* Close other applications; on laptops use mains power and a fixed power plan.
* Run at least 5 repetitions; prefer the median and look at min/max. A single run supports no conclusion.
* Compare strategies **within one run** on the same machine. Do not compare absolute numbers across machines.
* Wall-clock differences smaller than the run-to-run spread are not findings.
* Latency includes timer overhead (`timer_overhead_ns`); for sub-microsecond operations that is a significant share.

## Recorded metadata

Every JSON report stores: `project_version`, `git_commit`, `compiler_name`, `compiler_version`, `build_type`,
`operating_system`, `cpu_description`, `hardware_threads`, `generated_at_utc`, the full effective workload config
(including `seed`, `heap_size_bytes`, `operations`), `warmup_operations`, `repetitions`, `latency_unit`,
`trace_checksum_fnv1a64`, and the per-repetition values behind every summary statistic. CSV files repeat the
build type, compiler and commit on every row.

`git_commit` is captured when CMake *configures*; re-run the configure step after committing, or the value is stale.

## Validation before trusting results

`memoryforge validate --all` replays every shipped config against every strategy/policy with invariant checks, verifies
that the same seed reproduces the same trace, that the arena drains back to a single block, that OPT never loses to
another policy, and that the hand-verified reference fault counts (FIFO 10 / LRU 9 / OPT 7 on the 13-reference
textbook trace, Belady's anomaly 9 -> 10) still hold.
