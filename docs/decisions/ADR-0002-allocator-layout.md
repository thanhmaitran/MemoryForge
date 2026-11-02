# ADR-0002: Out-of-band metadata and an arena owned by the allocator

* Status: accepted

## Context

The allocator must be safe to experiment with, easy to validate and reproducible. Three layout choices were
considered:

1. in-band headers/footers inside the arena (classic boundary tags),
2. out-of-band metadata in standard containers, with the arena holding only user data,
3. replacing the process-wide `malloc` / `operator new`.

## Decision

Option 2, with one arena obtained once from the host and aligned to 4096 bytes.

* Offsets and sizes are multiples of 16 bytes; no per-block header is charged to the user.
* The free list is an address-ordered `std::map`, live blocks live in a hash map keyed by allocation id.
* Handles are `(id, offset)` pairs; `deallocate` takes the id, which makes invalid and double frees detectable.
* The global heap is never replaced.

## Why

* A bug in user code cannot corrupt allocator metadata, and tests can fill allocations with patterns to detect
  overlap.
* Detecting `InvalidHandle` and `DoubleFree` is exact, which is impossible with raw pointers into in-band headers.
* All three strategies share the same free list, splitting and coalescing code, so a comparison measures the
  selection rule rather than implementation differences.
* Replacing the global allocator would make experiments unsafe and hard to reproduce.

## Consequences

* Latency includes the cost of `std::map` / `unordered_map` node allocation. Absolute numbers are higher than for an
  intrusive free list; relative comparisons between strategies stay meaningful because the overhead is shared.
* No header overhead means internal overhead measures granularity rounding only. A header-based variant would need
  its own overhead definition before results could be compared.
* A future intrusive or size-class implementation can be added behind the same `MemoryAllocator` interface.
