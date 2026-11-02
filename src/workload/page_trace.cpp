#include "memoryforge/workload/page_trace.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

#include "memoryforge/common/io.hpp"
#include "memoryforge/common/rng.hpp"

namespace memoryforge {

std::vector<Vpn> PageTrace::vpns() const {
    std::vector<Vpn> out;
    out.reserve(accesses.size());
    for (const PageAccess& a : accesses) out.push_back(a.vpn);
    return out;
}

std::uint64_t PageTrace::checksum() const noexcept {
    Fnv1a h;
    for (const PageAccess& a : accesses) {
        h.add(a.vpn);
        h.add(a.write ? 1U : 0U);
    }
    return h.value();
}

std::uint64_t virtual_address_for(const PageAccess& access, std::uint64_t index, std::uint64_t page_size) noexcept {
    return access.vpn * page_size + (index * 64U) % page_size;
}

Result<PageTrace> parse_page_trace(std::string_view text) {
    PageTrace trace;
    std::istringstream in{std::string(text)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream tokens(line);
        std::string token;
        while (tokens >> token) {
            bool write = false;
            if (token.back() == 'w' || token.back() == 'W') {
                write = true;
                token.pop_back();
            }
            std::uint64_t vpn = 0;
            const auto* end = token.data() + token.size();
            const auto r = std::from_chars(token.data(), end, vpn);
            if (token.empty() || r.ec != std::errc{} || r.ptr != end) {
                return make_error(ErrorCode::ParseError, "page trace line " + std::to_string(line_number) +
                                                             ": invalid page number '" + token + "'");
            }
            trace.accesses.push_back({vpn, write});
        }
    }
    return trace;
}

std::string format_page_trace(const PageTrace& trace) {
    std::ostringstream out;
    out << "# MemoryForge page-access trace; 'w' marks a write\n";
    for (std::size_t i = 0; i < trace.accesses.size(); ++i) {
        out << trace.accesses[i].vpn << (trace.accesses[i].write ? "w" : "") << ((i + 1) % 16 == 0 ? '\n' : ' ');
    }
    out << '\n';
    return out.str();
}

std::string_view to_string(PagePattern p) noexcept {
    switch (p) {
        case PagePattern::Sequential: return "sequential";
        case PagePattern::RepeatedLoop: return "repeated_loop";
        case PagePattern::WorkingSetShift: return "working_set_shift";
        case PagePattern::Random: return "random";
        case PagePattern::Adversarial: return "adversarial";
    }
    return "unknown";
}

std::optional<PagePattern> parse_pattern(std::string_view name) noexcept {
    for (const PagePattern p : {PagePattern::Sequential, PagePattern::RepeatedLoop, PagePattern::WorkingSetShift,
                                PagePattern::Random, PagePattern::Adversarial}) {
        if (name == to_string(p)) return p;
    }
    return std::nullopt;
}

namespace {
std::uint64_t effective_adversarial_frames(const VmWorkloadConfig& c) {
    if (c.adversarial_frames != 0) return c.adversarial_frames;
    return c.frames.empty() ? 1 : *std::min_element(c.frames.begin(), c.frames.end());
}
bool in_unit_interval(double v) { return v >= 0.0 && v <= 1.0; }
}  // namespace

Status validate_config(const VmWorkloadConfig& c) {
    auto bad = [](const std::string& why) { return Status(make_error(ErrorCode::InvalidConfig, why)); };
    if (c.schema_version != 1) return bad("unsupported schema_version (expected 1)");
    if (c.page_size == 0 || (c.page_size & (c.page_size - 1)) != 0) return bad("page_size must be a power of two");
    if (c.virtual_pages == 0 || c.virtual_pages > (std::uint64_t{1} << 24)) return bad("virtual_pages must be in [1, 2^24]");
    if (c.frames.empty()) return bad("frames must list at least one frame count");
    for (const std::uint64_t f : c.frames) {
        if (f == 0) return bad("every frame count must be non-zero");
    }
    if (c.accesses == 0) return bad("accesses must be at least 1");
    if (!in_unit_interval(c.write_probability)) return bad("write_probability must be in [0, 1]");
    if (c.loop_pages == 0 || c.loop_pages > c.virtual_pages) return bad("loop_pages must be in [1, virtual_pages]");
    if (c.working_set_pages == 0 || c.working_set_pages > c.virtual_pages) return bad("working_set_pages must be in [1, virtual_pages]");
    if (c.phase_length == 0) return bad("phase_length must be at least 1");
    if (c.pattern == PagePattern::Adversarial && effective_adversarial_frames(c) + 1 > c.virtual_pages) {
        return bad("adversarial pattern needs virtual_pages > adversarial_frames");
    }
    if (c.repetitions == 0) return bad("repetitions must be at least 1");
    return Status::success();
}

Result<VmWorkloadConfig> parse_vm_config(const json::Value& root) {
    VmWorkloadConfig c;
    std::string pattern = std::string(to_string(c.pattern));
    json::ObjectReader reader(root, "virtual-memory workload config");
    reader.read("schema_version", c.schema_version)
        .read("workload_id", c.workload_id)
        .read("name", c.name)
        .read("seed", c.seed)
        .read("pattern", pattern)
        .read("page_size", c.page_size)
        .read("virtual_pages", c.virtual_pages)
        .read("frames", c.frames)
        .read("accesses", c.accesses)
        .read("write_probability", c.write_probability)
        .read("loop_pages", c.loop_pages)
        .read("working_set_pages", c.working_set_pages)
        .read("phase_length", c.phase_length)
        .read("adversarial_frames", c.adversarial_frames)
        .read("repetitions", c.repetitions);
    if (Status s = reader.finish(); !s) return s.error();
    const auto parsed = parse_pattern(pattern);
    if (!parsed) {
        return make_error(ErrorCode::InvalidConfig,
                          "pattern must be sequential, repeated_loop, working_set_shift, random or adversarial");
    }
    c.pattern = *parsed;
    if (Status s = validate_config(c); !s) return s.error();
    return c;
}

Result<VmWorkloadConfig> load_vm_config(const std::filesystem::path& path) {
    auto text = read_text_file(path);
    if (!text) return text.error();
    auto doc = json::parse(text.value());
    if (!doc) return make_error(doc.error().code, path.string() + ": " + doc.error().message);
    auto config = parse_vm_config(doc.value());
    if (!config) return make_error(config.error().code, path.string() + ": " + config.error().message);
    return config;
}

void write_config_json(json::Writer& w, const VmWorkloadConfig& c) {
    w.begin_object();
    w.key("schema_version"); w.value(c.schema_version);
    w.key("workload_id"); w.value(c.workload_id);
    w.key("name"); w.value(c.name);
    w.key("seed"); w.value(c.seed);
    w.key("pattern"); w.value(to_string(c.pattern));
    w.key("page_size"); w.value(c.page_size);
    w.key("virtual_pages"); w.value(c.virtual_pages);
    w.key("frames");
    w.begin_array();
    for (const std::uint64_t f : c.frames) w.value(f);
    w.end_array();
    w.key("accesses"); w.value(c.accesses);
    w.key("write_probability"); w.value(c.write_probability);
    w.key("loop_pages"); w.value(c.loop_pages);
    w.key("working_set_pages"); w.value(c.working_set_pages);
    w.key("phase_length"); w.value(c.phase_length);
    w.key("adversarial_frames"); w.value(c.adversarial_frames);
    w.key("repetitions"); w.value(c.repetitions);
    w.end_object();
}

Result<PageTrace> generate_page_trace(const VmWorkloadConfig& c) {
    if (Status s = validate_config(c); !s) return s.error();
    Rng rng(c.seed);
    PageTrace trace;
    trace.accesses.reserve(static_cast<std::size_t>(c.accesses));
    const std::uint64_t cycle = effective_adversarial_frames(c) + 1;
    const std::uint64_t windows = std::max<std::uint64_t>(1, c.virtual_pages / c.working_set_pages);
    for (std::uint64_t i = 0; i < c.accesses; ++i) {
        Vpn vpn = 0;
        switch (c.pattern) {
            case PagePattern::Sequential: vpn = i % c.virtual_pages; break;
            case PagePattern::RepeatedLoop: vpn = i % c.loop_pages; break;
            case PagePattern::WorkingSetShift:
                vpn = ((i / c.phase_length) % windows) * c.working_set_pages + rng.uniform(0, c.working_set_pages - 1);
                break;
            case PagePattern::Random: vpn = rng.uniform(0, c.virtual_pages - 1); break;
            case PagePattern::Adversarial: vpn = i % cycle; break;
        }
        trace.accesses.push_back({vpn, rng.chance(c.write_probability)});
    }
    return trace;
}

}  // namespace memoryforge
