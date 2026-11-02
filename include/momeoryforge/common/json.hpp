#pragma once

#include <concepts>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memoryforge/common/result.hpp"

/// Minimal JSON support: a strict recursive-descent parser for configuration
/// files and a small streaming writer for reports. No external dependency is
/// needed, which keeps the project buildable from a clean clone.
namespace memoryforge::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Members = std::vector<std::pair<std::string, Value>>;

    Value() = default;

    static Value make_bool(bool b) {
        Value v;
        v.type_ = Type::Bool;
        v.bool_ = b;
        return v;
    }
    static Value make_number(double d, bool is_uint, std::uint64_t u) {
        Value v;
        v.type_ = Type::Number;
        v.number_ = d;
        v.is_uint_ = is_uint;
        v.uint_ = u;
        return v;
    }
    static Value make_string(std::string s) {
        Value v;
        v.type_ = Type::String;
        v.string_ = std::move(s);
        return v;
    }
    static Value make_array(std::vector<Value> items) {
        Value v;
        v.type_ = Type::Array;
        v.items_ = std::move(items);
        return v;
    }
    static Value make_object(Members members) {
        Value v;
        v.type_ = Type::Object;
        v.members_ = std::move(members);
        return v;
    }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Object; }
    /// True when the number was written as a non-negative integer that fits in 64 bits.
    [[nodiscard]] bool is_uint() const noexcept { return type_ == Type::Number && is_uint_; }

    [[nodiscard]] bool as_bool() const noexcept { return bool_; }
    [[nodiscard]] double as_double() const noexcept { return number_; }
    [[nodiscard]] std::uint64_t as_uint() const noexcept { return uint_; }
    [[nodiscard]] const std::string& as_string() const noexcept { return string_; }
    [[nodiscard]] const std::vector<Value>& items() const noexcept { return items_; }
    [[nodiscard]] const Members& members() const noexcept { return members_; }

    [[nodiscard]] const Value* find(std::string_view key) const noexcept {
        for (const auto& member : members_) {
            if (member.first == key) return &member.second;
        }
        return nullptr;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    bool is_uint_ = false;
    double number_ = 0.0;
    std::uint64_t uint_ = 0;
    std::string string_;
    std::vector<Value> items_;
    Members members_;
};

/// Parses a complete JSON document. Errors report line and column.
Result<Value> parse(std::string_view text);

/// Streaming pretty-printer. Callers are responsible for well-formed nesting.
class Writer {
public:
    explicit Writer(std::ostream& out, int indent = 2) : out_(out), indent_(indent) {}

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();
    void key(std::string_view name);
    void null();
    void value(bool b);
    void value(std::string_view s);
    void value(const char* s) { value(std::string_view{s}); }
    void value(const std::string& s) { value(std::string_view{s}); }
    /// Non-finite values are written as null because JSON cannot represent them.
    void value(double d);

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    void value(T v) {
        prefix();
        out_ << v;
    }

private:
    struct Frame {
        std::size_t count = 0;
    };
    void prefix();
    void newline();
    void write_string(std::string_view s);

    std::ostream& out_;
    int indent_;
    bool after_key_ = false;
    std::vector<Frame> stack_;
};

/// Reads typed fields out of a JSON object and reports unknown keys, so a
/// typo in a configuration file is an error instead of a silently ignored key.
class ObjectReader {
public:
    ObjectReader(const Value& value, std::string context);

    ObjectReader& read(std::string_view key, std::string& out);
    ObjectReader& read(std::string_view key, std::uint64_t& out);
    ObjectReader& read(std::string_view key, double& out);
    ObjectReader& read(std::string_view key, bool& out);
    ObjectReader& read(std::string_view key, std::vector<std::uint64_t>& out);

    /// Returns the first type error, or an error naming any key never read.
    Status finish();

private:
    const Value* take(std::string_view key);
    void fail(std::string_view key, std::string_view expected);

    const Value* object_;
    std::string context_;
    std::vector<std::string> consumed_;
    std::optional<Error> first_error_;
};

}  // namespace memoryforge::json
