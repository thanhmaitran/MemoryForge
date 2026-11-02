#include "memoryforge/workload/allocation_trace.hpp"

#include <charconv>
#include <sstream>

#include "memoryforge/common/rng.hpp"

namespace memoryforge {

std::size_t AllocationTrace::allocate_count() const noexcept {
    std::size_t n = 0;
    for (const AllocOp& op : ops) n += (op.kind == AllocOp::Kind::Allocate) ? 1U : 0U;
    return n;
}

std::uint64_t AllocationTrace::checksum() const noexcept {
    Fnv1a h;
    for (const AllocOp& op : ops) {
        h.add(static_cast<std::uint64_t>(op.kind));
        h.add(op.ordinal);
        h.add(static_cast<std::uint64_t>(op.size));
        h.add(static_cast<std::uint64_t>(op.alignment));
    }
    return h.value();
}

Status validate_trace(const AllocationTrace& trace) {
    std::vector<bool> freed;
    for (std::size_t i = 0; i < trace.ops.size(); ++i) {
        const AllocOp& op = trace.ops[i];
        if (op.kind == AllocOp::Kind::Allocate) {
            if (op.size == 0) {
                return make_error(ErrorCode::InvalidArgument, "op " + std::to_string(i) + ": zero-size allocation");
            }
            freed.push_back(false);
        } else {
            if (op.ordinal >= freed.size()) {
                return make_error(ErrorCode::InvalidArgument,
                                  "op " + std::to_string(i) + ": free of an allocation that has not happened yet");
            }
            if (freed[static_cast<std::size_t>(op.ordinal)]) {
                return make_error(ErrorCode::InvalidArgument, "op " + std::to_string(i) + ": allocation freed twice");
            }
            freed[static_cast<std::size_t>(op.ordinal)] = true;
        }
    }
    return Status::success();
}

namespace {
bool parse_u64(std::string_view token, std::uint64_t& out) {
    const auto* end = token.data() + token.size();
    const auto result = std::from_chars(token.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end;
}
}  // namespace

Result<AllocationTrace> parse_allocation_trace(std::string_view text) {
    AllocationTrace trace;
    std::istringstream in{std::string(text)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        std::istringstream fields(line);
        std::string kind;
        if (!(fields >> kind) || kind[0] == '#') continue;
        auto bad = [&](const std::string& why) {
            return make_error(ErrorCode::ParseError, "trace line " + std::to_string(line_number) + ": " + why);
        };
        std::string a;
        std::string b;
        std::uint64_t x = 0;
        std::uint64_t y = 0;
        if (kind == "A") {
            if (!(fields >> a >> b) || !parse_u64(a, x) || !parse_u64(b, y)) return bad("expected 'A <size> <alignment>'");
            trace.ops.push_back({AllocOp::Kind::Allocate, 0, static_cast<std::size_t>(x), static_cast<std::size_t>(y)});
        } else if (kind == "F") {
            if (!(fields >> a) || !parse_u64(a, x)) return bad("expected 'F <ordinal>'");
            trace.ops.push_back({AllocOp::Kind::Free, x, 0, 0});
        } else {
            return bad("unknown op '" + kind + "'");
        }
        if (fields >> a) return bad("unexpected trailing field");
    }
    // Allocate ordinals are implicit; fill them in for convenience.
    std::uint64_t ordinal = 0;
    for (AllocOp& op : trace.ops) {
        if (op.kind == AllocOp::Kind::Allocate) op.ordinal = ordinal++;
    }
    if (Status s = validate_trace(trace); !s) return s.error();
    return trace;
}

std::string format_allocation_trace(const AllocationTrace& trace) {
    std::ostringstream out;
    out << "# MemoryForge allocation trace: A <size> <alignment> | F <ordinal>\n";
    for (const AllocOp& op : trace.ops) {
        if (op.kind == AllocOp::Kind::Allocate) {
            out << "A " << op.size << ' ' << op.alignment << '\n';
        } else {
            out << "F " << op.ordinal << '\n';
        }
    }
    return out.str();
}

}  // namespace memoryforge
