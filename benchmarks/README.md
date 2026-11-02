# Benchmarks

Workload definitions live in [`configs/`](../configs); the measurement code is in
[`src/benchmark/`](../src/benchmark). This directory holds the orchestration script.

```text
python benchmarks/scripts/run_all.py --exe <path to memoryforge> --out reports/local --repetitions 5
python analysis/plot_results.py --reports reports/local --out reports/local/charts
```

Read [the benchmark protocol](../docs/experiments/benchmark-protocol.md) before interpreting any number.

| Workload | Config | What it probes |
| --- | --- | --- |
| W01 | `uniform-random.json` | baseline behaviour |
| W02 | `mixed-lifetimes.json` | long-lived blocks pinning holes open |
| W03 | `burst-allocation.json` | splitting, coalescing and reuse after bursts |
| W04 | `size-skew.json` | large requests against many small ones |
| W05 | `long-running-stress.json` | 200,000 operations with alternating alloc/free-heavy phases |
| V01 | `sequential.json` | scan; compulsory faults dominate |
| V02 | `repeated-loop.json` | loop that does / does not fit in the frames |
| V03 | `working-set-shift.json` | adaptation when locality moves |
| V04 | `random-access.json` | low locality |
| V05 | `adversarial.json` | cyclic scan of frames+1 pages: worst case for FIFO/LRU/Clock |
