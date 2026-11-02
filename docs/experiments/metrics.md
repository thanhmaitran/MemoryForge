# Metrics

Every metric is either **algorithmic** (a deterministic function of the trace and the algorithm, identical on every
machine) or **wall-clock** (depends on hardware, compiler, build type and system load). Reports keep them apart and
the two kinds must not be mixed when drawing conclusions.

## Allocator

| Metric | Kind | Definition |
| --- | --- | --- |
| `allocate_attempts` | algorithmic | allocate operations in the trace |
| `allocations_failed`, `failure_rate` | algorithmic | valid requests answered with `OutOfMemory`; rate = failed / attempts |
| `frees_skipped` | algorithmic | frees of allocations that had failed (nothing to release) |
| `search_steps`, `search_steps_per_attempt` | algorithmic | free blocks examined while searching; the machine-independent cost of a strategy |
| `final_free_bytes`, `final_largest_free_block`, `final_free_block_count` | algorithmic | state after the whole trace |
| `final_external_fragmentation` | algorithmic | `1 - L/F` with `L` = largest free block, `F` = total free bytes. **Empty (undefined) when `F = 0`.** An indicator of free-space layout, not a universal definition of fragmentation |
| `final_internal_overhead_ratio` | algorithmic | `(allocated - requested) / allocated` over live blocks; granularity rounding only (no headers). Empty when nothing is allocated |
| `peak_live_allocated_bytes` | algorithmic | highest reserved byte count during the run |
| `external_fragmentation` (timeseries) | algorithmic | the indicator sampled every `sample_interval` operations |
| `throughput_*_ops_per_s` | wall-clock | operations per second over the measured region (warm-up excluded), per repetition; median / min / max reported |
| `alloc_latency_*`, `free_latency_*` | wall-clock | per-call latency in nanoseconds: min, mean, median, p95, p99, max |
| `timer_overhead_ns` | wall-clock | median cost of two back-to-back clock reads; **included** in latency samples, not subtracted |

Percentiles use the nearest-rank method. `p95` needs at least 20 samples and `p99` at least 100; otherwise the field is
empty rather than extrapolated.

### Why the indicator can mislead

Two heaps with the same free bytes can differ completely in usefulness (one 1024-byte hole versus eight 128-byte
holes). The indicator captures that, but it says nothing about the *request-size distribution*: a heap with indicator
0.9 can still serve every request in a workload of tiny allocations. Read it together with `failure_rate`.

## Virtual memory

| Metric | Kind | Definition |
| --- | --- | --- |
| `accesses`, `hits`, `page_faults`, `fault_rate` | algorithmic | `fault_rate = page_faults / accesses` |
| `compulsory_faults` | algorithmic | first-ever reference to a page; a floor no policy can avoid |
| `replacements` | algorithmic | faults that had to evict a resident page (`page_faults - frames filled`) |
| `writebacks` | algorithmic | evictions of dirty pages |
| `metadata_bytes` | algorithmic | page table + frame owner table + history bits of the simulator |
| `accesses_per_s_*` | wall-clock | simulator speed; says nothing about a real MMU |

OPT is an offline lower bound on page faults for a given trace and frame count. It is a reference line, not a
competitor with the same information as the online policies.
