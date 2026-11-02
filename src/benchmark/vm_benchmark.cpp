#include "memoryforge/benchmark/vm_benchmark.hpp"

#include <algorithm>
#include <string>

#include "memoryforge/metrics/latency.hpp"

namespace memoryforge {
namespace {

Result<VirtualMemory> build_vm(const VmWorkloadConfig& config, const std::vector<Vpn>& vpns,
                               ReplacementPolicyKind kind, std::uint64_t frames) {
    auto policy = make_policy(kind, frames, vpns);
    if (!policy) return policy.error();
    return VirtualMemory::create(VmConfig{config.page_size, config.virtual_pages, frames}, std::move(policy).value());
}

}  // namespace

Result<VmRunResult> run_vm_benchmark(const VmWorkloadConfig& config, const PageTrace& trace,
                                     ReplacementPolicyKind kind, std::uint64_t frames,
                                     const VmRunOptions& options) {
    const std::vector<Vpn> vpns = trace.vpns();
    VmRunResult result;
    result.policy = kind;
    result.frames = frames;

    {  // Deterministic pass: counters and invariants.
        auto vm = build_vm(config, vpns, kind, frames);
        if (!vm) return vm.error();
        for (std::size_t i = 0; i < trace.accesses.size(); ++i) {
            const PageAccess& a = trace.accesses[i];
            const auto r = vm.value().access(virtual_address_for(a, i, config.page_size),
                                             a.write ? AccessType::Write : AccessType::Read);
            if (!r) return r.error();
            const std::uint64_t done = i + 1;
            if (options.validate_interval != 0 &&
                (done % options.validate_interval == 0 || done == trace.accesses.size())) {
                ++result.validation_checks;
                if (const auto problem = vm.value().diagnose()) {
                    return make_error(ErrorCode::ValidationFailed,
                                      "VM invariant violated after access " + std::to_string(i) + " (" +
                                          std::string(to_string(kind)) + ", " + std::to_string(frames) +
                                          " frames): " + *problem);
                }
            }
        }
        result.stats = vm.value().stats();
        result.metadata_bytes = vm.value().metadata_bytes();
    }

    for (std::uint64_t rep = 0; rep < options.repetitions; ++rep) {
        auto vm = build_vm(config, vpns, kind, frames);  // construction is not timed
        if (!vm) return vm.error();
        const std::uint64_t t0 = now_ns();
        for (std::size_t i = 0; i < trace.accesses.size(); ++i) {
            const PageAccess& a = trace.accesses[i];
            static_cast<void>(vm.value().access(virtual_address_for(a, i, config.page_size),
                                                a.write ? AccessType::Write : AccessType::Read));
        }
        const std::uint64_t t1 = now_ns();
        const double seconds = static_cast<double>(std::max<std::uint64_t>(t1 - t0, 1)) * 1e-9;
        result.accesses_per_sec.push_back(static_cast<double>(trace.accesses.size()) / seconds);
    }
    return result;
}

}  // namespace memoryforge
