# Contributing

## Workflow

1. Create a branch, keep commits small and descriptive (`allocator: coalesce with predecessor on free`).
2. Build with warnings as errors and run the full suite before pushing:
   ```bash
   cmake -S . -B build -DMEMORYFORGE_WARNINGS_AS_ERRORS=ON && cmake --build build && ctest --test-dir build --output-on-failure
   cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # Linux/macOS
   ```
3. Format with `clang-format` (`.clang-format`) and fix `clang-tidy` findings in code you touch.
4. Open a pull request. CI runs GCC, Clang, MSVC and the sanitizer jobs.

## Rules that protect the project's credibility

* **Every behaviour change needs a test**, including the failure path. Tests check the contract, not the
  implementation's internals.
* **New allocator or policy:** add it behind `MemoryAllocator` / `ReplacementPolicy`, extend `validate()` / `diagnose()`
  if it adds state, and make sure it uses the same layout convention (or document and justify the difference) so that
  comparisons stay fair.
* **Never commit unverified benchmark numbers.** A committed report needs its JSON metadata (compiler, build type, OS,
  CPU, seed, commit) and must come from a Release build. State where it was produced. Do not mix Debug and Release.
* **README and docs describe only what exists.** Planned features go in the roadmap, not in the feature list.
* Do not commit `build/`, binaries, caches or large generated logs. Large workloads are regenerated from seeds.

## Adding a workload

Add a JSON file under `configs/allocator/` or `configs/virtual-memory/` (unknown fields are rejected, so typos fail
loudly), run `memoryforge validate --config <file>`, and mention it in `benchmarks/README.md`.

## Changing the PRNG or generators

Any change that alters generated traces invalidates committed reports. Update `rng_golden_values_*` knowingly,
regenerate `reports/` and say so in the commit message.
