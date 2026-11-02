// Walks a tiny reference string through the VM simulator and prints each translation.
#include <iostream>

#include "memoryforge/vm/virtual_memory.hpp"

using namespace memoryforge;

int main() {
    const VmConfig config{4096, 16, 3};  // 4 KiB pages, 16 virtual pages, 3 physical frames
    auto vm = VirtualMemory::create(config, make_policy(ReplacementPolicyKind::Lru, config.frames).value());
    if (!vm) {
        std::cerr << vm.error().message << '\n';
        return 1;
    }

    const std::uint64_t pages[] = {7, 0, 1, 2, 0, 3, 0, 4};
    for (const std::uint64_t page : pages) {
        const std::uint64_t va = page * config.page_size + 0x2A;
        const auto r = vm.value().access(va, AccessType::Read).value();
        std::cout << "VA 0x" << std::hex << va << std::dec << " -> VPN " << r.vpn << ", offset " << r.offset << " -> ";
        if (r.page_fault) {
            std::cout << "PAGE FAULT";
            if (r.evicted) std::cout << " (evict VPN " << *r.evicted << ")";
            std::cout << ", loaded into frame " << r.pfn;
        } else {
            std::cout << "hit, frame " << r.pfn;
        }
        std::cout << ", PA 0x" << std::hex << r.physical_address << std::dec << '\n';
    }
    const VmStats& s = vm.value().stats();
    std::cout << s.page_faults << " faults / " << s.accesses << " accesses\n";
    return vm.value().validate() ? 0 : 1;
}
