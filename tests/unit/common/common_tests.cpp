#include <cmath>
#include <sstream>

#include "memoryforge/common/json.hpp"
#include "memoryforge/metrics/fragmentation.hpp"
#include "memoryforge/metrics/latency.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

TEST(json_parses_all_value_types) {
    auto v = json::parse(R"({"a": [1, 2.5, -3, true, false, null], "s": "x\n\"y\u0041", "o": {"k": 18446744073709551615}})");
    CHECK(v.ok());
    const auto& root = v.value();
    const auto* a = root.find("a");
    CHECK(a != nullptr && a->is_array() && a->items().size() == 6);
    CHECK(a->items()[0].is_uint());
    CHECK(!a->items()[1].is_uint());
    CHECK(!a->items()[2].is_uint());  // negative
    CHECK(a->items()[3].as_bool());
    CHECK(a->items()[5].is_null());
    CHECK_EQ(root.find("s")->as_string(), std::string("x\n\"yA"));
    CHECK_EQ(root.find("o")->find("k")->as_uint(), 18446744073709551615ULL);  // exact above 2^53
}

TEST(json_rejects_malformed_documents) {
    for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "{\"a\":1 \"b\":2}", "01", "1.", "\"unterminated", "{\"a\":1,\"a\":2}",
                            "tru", "[1] x", "{'a':1}", "\"\\q\"", "-"}) {
        CHECK(!json::parse(bad).ok());
    }
    CHECK(!json::parse(std::string(200, '[')).ok());  // nesting limit
    auto e = json::parse("{\n  \"a\": ?\n}");
    CHECK(!e.ok());
    CHECK(e.error().message.find("line 2") != std::string::npos);
}

TEST(json_writer_output_parses_back) {
    std::ostringstream os;
    {
        json::Writer w(os);
        w.begin_object();
        w.key("text"); w.value("quote\" backslash\\ newline\n");
        w.key("n"); w.value(std::uint64_t{18446744073709551615ULL});
        w.key("pi"); w.value(3.25);
        w.key("nan"); w.value(std::nan(""));
        w.key("list");
        w.begin_array();
        w.value(1);
        w.value(true);
        w.null();
        w.end_array();
        w.key("empty"); w.begin_object(); w.end_object();
        w.end_object();
    }
    auto back = json::parse(os.str());
    CHECK(back.ok());
    CHECK_EQ(back.value().find("text")->as_string(), std::string("quote\" backslash\\ newline\n"));
    CHECK_EQ(back.value().find("n")->as_uint(), 18446744073709551615ULL);
    CHECK(back.value().find("nan")->is_null());
    CHECK_EQ(back.value().find("list")->items().size(), 3U);
}

TEST(latency_summary_uses_nearest_rank) {
    std::vector<std::uint64_t> samples;
    for (std::uint64_t i = 1; i <= 100; ++i) samples.push_back(i);
    const auto s = summarize_latency(samples);
    CHECK_EQ(s.count, 100U);
    CHECK(s.min == 1.0 && s.max == 100.0);
    CHECK(s.median == 50.0);
    CHECK(s.p95 == 95.0);
    CHECK(s.p99 == 99.0);
    CHECK(s.mean == 50.5);
    const auto few = summarize_latency({5, 1, 3});
    CHECK(few.median == 3.0);
    CHECK(std::isnan(few.p95) && std::isnan(few.p99));  // too few samples to claim a tail
    CHECK(std::isnan(summarize_latency({}).median));
    CHECK(median_of({4.0, 1.0, 3.0, 2.0}) == 2.5);
}

TEST(fragmentation_metrics_handle_empty_cases) {
    AllocatorStats s;
    s.capacity_bytes = 1000;
    CHECK(!external_fragmentation_indicator(s).has_value());  // no free bytes
    CHECK(!internal_overhead_ratio(s).has_value());           // nothing allocated
    s.free_bytes = 1000;
    s.largest_free_block = 1000;
    CHECK(*external_fragmentation_indicator(s) == 0.0);
    s.largest_free_block = 250;
    CHECK(*external_fragmentation_indicator(s) == 0.75);
    s.live_allocated_bytes = 100;
    s.live_requested_bytes = 90;
    CHECK(*internal_overhead_ratio(s) == 0.1);
}
