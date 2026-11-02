#include "memoryforge/common/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace memoryforge::json {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Value> parse_document() {
        skip_ws();
        auto value = parse_value(0);
        if (!value) return value;
        skip_ws();
        if (pos_ != text_.size()) return fail("unexpected characters after the JSON document");
        return value;
    }

private:
    Error fail(const std::string& message) const {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return make_error(ErrorCode::ParseError, "JSON error at line " + std::to_string(line) +
                                                     ", column " + std::to_string(column) + ": " +
                                                     message);
    }

    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const { return text_[pos_]; }

    void skip_ws() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
            ++pos_;
        }
    }

    bool consume_literal(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    Result<Value> parse_value(int depth) {
        if (depth > kMaxDepth) return fail("nesting is too deep");
        if (at_end()) return fail("unexpected end of input");
        const char c = peek();
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') {
            auto s = parse_string();
            if (!s) return s.error();
            return Value::make_string(std::move(s).value());
        }
        if (c == 't') {
            if (consume_literal("true")) return Value::make_bool(true);
            return fail("invalid literal");
        }
        if (c == 'f') {
            if (consume_literal("false")) return Value::make_bool(false);
            return fail("invalid literal");
        }
        if (c == 'n') {
            if (consume_literal("null")) return Value{};
            return fail("invalid literal");
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        return fail(std::string("unexpected character '") + c + "'");
    }

    Result<Value> parse_object(int depth) {
        ++pos_;  // '{'
        Value::Members members;
        skip_ws();
        if (!at_end() && peek() == '}') {
            ++pos_;
            return Value::make_object(std::move(members));
        }
        while (true) {
            skip_ws();
            if (at_end() || peek() != '"') return fail("expected a string key");
            auto key = parse_string();
            if (!key) return key.error();
            skip_ws();
            if (at_end() || peek() != ':') return fail("expected ':' after key");
            ++pos_;
            skip_ws();
            auto value = parse_value(depth + 1);
            if (!value) return value;
            const bool duplicate = std::any_of(
                members.begin(), members.end(),
                [&](const auto& m) { return m.first == key.value(); });
            if (duplicate) return fail("duplicate key \"" + key.value() + "\"");
            members.emplace_back(std::move(key).value(), std::move(value).value());
            skip_ws();
            if (at_end()) return fail("unterminated object");
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return Value::make_object(std::move(members));
            }
            return fail("expected ',' or '}'");
        }
    }

    Result<Value> parse_array(int depth) {
        ++pos_;  // '['
        std::vector<Value> items;
        skip_ws();
        if (!at_end() && peek() == ']') {
            ++pos_;
            return Value::make_array(std::move(items));
        }
        while (true) {
            skip_ws();
            auto value = parse_value(depth + 1);
            if (!value) return value;
            items.push_back(std::move(value).value());
            skip_ws();
            if (at_end()) return fail("unterminated array");
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return Value::make_array(std::move(items));
            }
            return fail("expected ',' or ']'");
        }
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80U) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        }
    }

    Result<std::string> parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (at_end()) return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20U) return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_end()) return fail("unterminated escape sequence");
            const char e = text_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        cp <<= 4U;
                        if (h >= '0' && h <= '9') {
                            cp |= static_cast<unsigned>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            cp |= static_cast<unsigned>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            cp |= static_cast<unsigned>(h - 'A' + 10);
                        } else {
                            return fail("invalid hex digit in \\u escape");
                        }
                    }
                    // Surrogate halves are not combined; they become U+FFFD.
                    append_utf8(out, (cp >= 0xD800U && cp <= 0xDFFFU) ? 0xFFFDU : cp);
                    break;
                }
                default: return fail("invalid escape sequence");
            }
        }
    }

    Result<Value> parse_number() {
        const std::size_t start = pos_;
        bool integer_like = true;
        if (peek() == '-') {
            integer_like = false;  // negatives are never "uint"
            ++pos_;
        }
        const std::size_t digits_start = pos_;
        while (!at_end() && peek() >= '0' && peek() <= '9') ++pos_;
        if (pos_ == digits_start) return fail("expected digits in number");
        if (text_[digits_start] == '0' && pos_ - digits_start > 1) {
            return fail("leading zeros are not allowed");
        }
        if (!at_end() && peek() == '.') {
            integer_like = false;
            ++pos_;
            const std::size_t frac = pos_;
            while (!at_end() && peek() >= '0' && peek() <= '9') ++pos_;
            if (pos_ == frac) return fail("expected digits after decimal point");
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            integer_like = false;
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-')) ++pos_;
            const std::size_t exp = pos_;
            while (!at_end() && peek() >= '0' && peek() <= '9') ++pos_;
            if (pos_ == exp) return fail("expected digits in exponent");
        }
        const std::string token(text_.substr(start, pos_ - start));
        const double d = std::strtod(token.c_str(), nullptr);
        bool is_uint = false;
        std::uint64_t u = 0;
        if (integer_like) {
            errno = 0;
            u = std::strtoull(token.c_str(), nullptr, 10);
            is_uint = (errno != ERANGE);
        }
        return Value::make_number(d, is_uint, u);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

Result<Value> parse(std::string_view text) { return Parser(text).parse_document(); }

// ---------------------------------------------------------------- Writer ---

void Writer::newline() {
    out_ << '\n';
    out_ << std::string(static_cast<std::size_t>(indent_) * stack_.size(), ' ');
}

void Writer::prefix() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!stack_.empty()) {
        if (stack_.back().count++ > 0) out_ << ',';
        newline();
    }
}

void Writer::write_string(std::string_view s) {
    out_ << '"';
    for (const char c : s) {
        switch (c) {
            case '"': out_ << "\\\""; break;
            case '\\': out_ << "\\\\"; break;
            case '\n': out_ << "\\n"; break;
            case '\r': out_ << "\\r"; break;
            case '\t': out_ << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out_ << buf;
                } else {
                    out_ << c;
                }
        }
    }
    out_ << '"';
}

void Writer::begin_object() {
    prefix();
    out_ << '{';
    stack_.emplace_back();
}

void Writer::end_object() {
    const bool empty = stack_.back().count == 0;
    stack_.pop_back();
    if (!empty) newline();
    out_ << '}';
}

void Writer::begin_array() {
    prefix();
    out_ << '[';
    stack_.emplace_back();
}

void Writer::end_array() {
    const bool empty = stack_.back().count == 0;
    stack_.pop_back();
    if (!empty) newline();
    out_ << ']';
}

void Writer::key(std::string_view name) {
    if (stack_.back().count++ > 0) out_ << ',';
    newline();
    write_string(name);
    out_ << ": ";
    after_key_ = true;
}

void Writer::null() {
    prefix();
    out_ << "null";
}

void Writer::value(bool b) {
    prefix();
    out_ << (b ? "true" : "false");
}

void Writer::value(std::string_view s) {
    prefix();
    write_string(s);
}

void Writer::value(double d) {
    prefix();
    if (!std::isfinite(d)) {
        out_ << "null";
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", d);
    out_ << buf;
}

// ---------------------------------------------------------- ObjectReader ---

ObjectReader::ObjectReader(const Value& value, std::string context)
    : object_(&value), context_(std::move(context)) {
    if (!value.is_object()) {
        first_error_ = make_error(ErrorCode::InvalidConfig, context_ + " must be a JSON object");
    }
}

const Value* ObjectReader::take(std::string_view key) {
    if (first_error_ || !object_->is_object()) return nullptr;
    const Value* v = object_->find(key);
    if (v != nullptr) consumed_.emplace_back(key);
    return v;
}

void ObjectReader::fail(std::string_view key, std::string_view expected) {
    if (!first_error_) {
        first_error_ = make_error(ErrorCode::InvalidConfig, context_ + ": field \"" +
                                                                std::string(key) + "\" must be " +
                                                                std::string(expected));
    }
}

ObjectReader& ObjectReader::read(std::string_view key, std::string& out) {
    if (const Value* v = take(key)) {
        if (v->is_string()) {
            out = v->as_string();
        } else {
            fail(key, "a string");
        }
    }
    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, std::uint64_t& out) {
    if (const Value* v = take(key)) {
        if (v->is_uint()) {
            out = v->as_uint();
        } else {
            fail(key, "a non-negative integer");
        }
    }
    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, double& out) {
    if (const Value* v = take(key)) {
        if (v->is_number()) {
            out = v->as_double();
        } else {
            fail(key, "a number");
        }
    }
    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, bool& out) {
    if (const Value* v = take(key)) {
        if (v->is_bool()) {
            out = v->as_bool();
        } else {
            fail(key, "true or false");
        }
    }
    return *this;
}

ObjectReader& ObjectReader::read(std::string_view key, std::vector<std::uint64_t>& out) {
    if (const Value* v = take(key)) {
        std::vector<std::uint64_t> result;
        bool good = true;
        if (v->is_uint()) {
            result.push_back(v->as_uint());  // a bare integer is a one-element list
        } else if (v->is_array()) {
            for (const Value& item : v->items()) {
                if (item.is_uint()) {
                    result.push_back(item.as_uint());
                } else {
                    good = false;
                }
            }
        } else {
            good = false;
        }
        if (good) {
            out = std::move(result);
        } else {
            fail(key, "a non-negative integer or an array of non-negative integers");
        }
    }
    return *this;
}

Status ObjectReader::finish() {
    if (first_error_) return *first_error_;
    for (const auto& member : object_->members()) {
        if (std::find(consumed_.begin(), consumed_.end(), member.first) == consumed_.end()) {
            return make_error(ErrorCode::InvalidConfig,
                              context_ + ": unknown field \"" + member.first + "\"");
        }
    }
    return Status::success();
}

}  // namespace memoryforge::json
