#include "memoryforge/benchmark/allocator_benchmark.hpp"

#include <algorithm>
#include <string>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/metrics/fragmentation.hpp"

namespace memoryforge {
namespace {

using Ids = std::vector<std::uint64_t>;

/// Replays ops [from, to) without any instrumentation (used for throughput).
void replay_plain(MemoryAllocator& alloc, const AllocationTrace& trace, Ids& ids, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        const AllocOp& op = trace.ops[i];
        if (op.kind == AllocOp::Kind::Allocate) {
            const auto r = alloc.allocate(op.size, op.alignment);
            if (r) ids[static_cast<std::size_t>(op.ordinal)] = r.value().id;
        } else {
            std::uint64_t& id = ids[static_cast<std::size_t>(op.ordinal)];
            if (id != 0) {
                static_cast<void>(alloc.deallocate(id));
                id = 0;
            }
        }
    }
}

/// Replays ops [from, to) timing every allocate/free individually.
void replay_timed(MemoryAllocator& alloc, const AllocationTrace& trace, Ids& ids, std::size_t from, std::size_t to,
                  std::vector<std::uint64_t>& alloc_ns, std::vector<std::uint64_t>& free_ns) {
    for (std::size_t i = from; i < to; ++i) {
        const AllocOp& op = trace.ops[i];
        if (op.kind == AllocOp::Kind::Allocate) {
            const std::uint64_t t0 = now_ns();
            const auto r = alloc.allocate(op.size, op.alignment);
            const std::uint64_t t1 = now_ns();
            alloc_ns.push_back(t1 - t0);
            if (r) ids[static_cast<std::size_t>(op.ordinal)] = r.value().id;
        } else {
            std::uint64_t& id = ids[static_cast<std::size_t>(op.ordinal)];
            if (id != 0) {
                const std::uint64_t t0 = now_ns();
                static_cast<void>(alloc.deallocate(id));
                const std::uint64_t t1 = now_ns();
                free_ns.push_back(t1 - t0);
                id = 0;
            }
        }
    }
}

Error fail(const std::string& message) { return make_error(ErrorCode::ValidationFailed, message); }

}  // namespace

Result<AllocatorRunResult> run_allocator_benchmark(const AllocatorWorkloadConfig& config, const AllocationTrace& trace,
                                                   AllocationStrategy strategy, const AllocatorRunOptions& options) {
    if (Status s = validate_trace(trace); !s) return s.error();
    const auto heap = static_cast<std::size_t>(config.heap_size_bytes);
    const std::size_t total_ops = trace.ops.size();

    AllocatorRunResult result;
    result.strategy = strategy;
    result.operations = total_ops;

    // ---- Pass 1: deterministic, untimed, instrumented. -------------------
    {
        auto created = make_allocator(strategy, heap);
        if (!created) return created.error();
        MemoryAllocator& alloc = *created.value();
        Ids ids(trace.allocate_count(), 0);

        for (std::size_t i = 0; i < total_ops; ++i) {
            const AllocOp& op = trace.ops[i];
            if (op.kind == AllocOp::Kind::Allocate) {
                ++result.allocate_attempts;
                const auto r = alloc.allocate(op.size, op.alignment);
                if (r) {
                    ids[static_cast<std::size_t>(op.ordinal)] = r.value().id;
                    ++result.allocations_succeeded;
                } else if (r.error().code == ErrorCode::OutOfMemory) {
                    ++result.allocations_failed;
                } else {
                    return fail("op " + std::to_string(i) + ": unexpected allocate error: " + r.error().message);
                }
            } else {
                std::uint64_t& id = ids[static_cast<std::size_t>(op.ordinal)];
                if (id == 0) {
                    ++result.frees_skipped;
                } else {
                    const Status s = alloc.deallocate(id);
                    if (!s) return fail("op " + std::to_string(i) + ": valid free rejected: " + s.error().message);
                    id = 0;
                    ++result.frees_executed;
                }
            }
            const std::uint64_t done = i + 1;
            if (options.sample_interval != 0 && (done % options.sample_interval == 0 || done == total_ops)) {
                result.timeseries.push_back({done, alloc.stats()});
            }
            if (options.validate_interval != 0 && (done % options.validate_interval == 0 || done == total_ops)) {
                ++result.validation_checks;
                if (const auto problem = alloc.diagnose()) {
                    return fail("invariant violated after op " + std::to_string(i) + " (" +
                                std::string(to_string(strategy)) + "): " + *problem);
                }
            }
        }
        result.final_stats = alloc.stats();

        if (options.verify_drain) {
            for (std::uint64_t& id : ids) {
                if (id == 0) continue;
                const Status s = alloc.deallocate(id);
                if (!s) return fail("drain: " + s.error().message);
                id = 0;
            }
            const AllocatorStats drained = alloc.stats();
            if (drained.live_allocation_count != 0 || drained.live_allocated_bytes != 0 ||
                drained.free_bytes != drained.capacity_bytes || drained.free_block_count != 1 ||
                drained.largest_free_block != drained.capacity_bytes) {
                return fail("drain: freeing every allocation did not restore a single free block (logical leak or missed coalescing)");
            }
            if (const auto problem = alloc.diagnose()) return fail("drain: " + *problem);
            result.drain_verified = true;
        }
    }

    // ---- Pass 2: wall-clock repetitions on fresh allocators. -------------
    if (options.repetitions > 0) {
        const std::size_t warmup = std::min<std::size_t>(static_cast<std::size_t>(options.warmup_operations), total_ops);
        result.timer_overhead_ns = measure_timer_overhead_ns();
        std::vector<std::uint64_t> alloc_samples;
        std::vector<std::uint64_t> free_samples;
        alloc_samples.reserve(static_cast<std::size_t>(options.repetitions) * total_ops);
        free_samples.reserve(static_cast<std::size_t>(options.repetitions) * total_ops);

        for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
            // Throughput: no per-op instrumentation, only two clock reads around the measured region.
            {
                auto created = make_allocator(strategy, heap);
                if (!created) return created.error();
                Ids ids(trace.allocate_count(), 0);
                replay_plain(*created.value(), trace, ids, 0, warmup);
                const std::uint64_t t0 = now_ns();
                replay_plain(*created.value(), trace, ids, warmup, total_ops);
                const std::uint64_t t1 = now_ns();
                const double seconds = static_cast<double>(std::max<std::uint64_t>(t1 - t0, 1)) * 1e-9;
                result.throughput_ops_per_sec.push_back(static_cast<double>(total_ops - warmup) / seconds);
            }
            // Latency: per-operation timing on its own fresh allocator.
            {
                auto created = make_allocator(strategy, heap);
                if (!created) return created.error();
                Ids ids(trace.allocate_count(), 0);
                replay_plain(*created.value(), trace, ids, 0, warmup);
                replay_timed(*created.value(), trace, ids, warmup, total_ops, alloc_samples, free_samples);
            }
        }
        result.alloc_latency = summarize_latency(std::move(alloc_samples));
        result.free_latency = summarize_latency(std::move(free_samples));
    }
    return result;
}

}  // namespace memoryforge
