// Minimal tour of the allocator API: allocate, inspect, free, observe coalescing.
#include <iostream>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/metrics/fragmentation.hpp"

using namespace memoryforge;

namespace {
void print(const char* label, const MemoryAllocator& a) {
    const AllocatorStats s = a.stats();
    std::cout << label << ": free=" << s.free_bytes << " largest_free=" << s.largest_free_block
              << " free_blocks=" << s.free_block_count << " fragmentation=";
    if (const auto f = external_fragmentation_indicator(s)) {
        std::cout << *f;
    } else {
        std::cout << "undefined (no free bytes)";
    }
    std::cout << '\n';
}
}  // namespace

int main() {
    auto created = make_allocator(AllocationStrategy::BestFit, 1024);
    if (!created) {
        std::cerr << created.error().message << '\n';
        return 1;
    }
    MemoryAllocator& heap = *created.value();

    auto a = heap.allocate(100, 16).value();
    auto b = heap.allocate(200, 64).value();
    auto c = heap.allocate(300, 16).value();
    std::cout << "a at " << a.offset << ", b at " << b.offset << " (64-byte aligned), c at " << c.offset << '\n';
    print("after 3 allocations", heap);

    static_cast<void>(heap.deallocate(b.id));
    print("after freeing b      ", heap);  // a hole appears between a and c
    static_cast<void>(heap.deallocate(a.id));
    print("after freeing a      ", heap);  // a's block merges with b's hole
    static_cast<void>(heap.deallocate(c.id));
    print("after freeing c      ", heap);  // everything coalesces into one block

    const auto again = heap.deallocate(c.id);
    std::cout << "freeing c twice -> " << to_string(again.error().code) << '\n';
    return heap.validate() ? 0 : 1;
}
