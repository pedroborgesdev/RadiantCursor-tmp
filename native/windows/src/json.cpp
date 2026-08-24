#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace rc::json {
namespace {

const Value nullValue;
const Value::Array emptyArray;
const Value::Object emptyObject;

class Parser {
public:
    Parser(std::string_view source, std::size_t maximumDepth) : source_(source), maximumDepth_(maximumDepth) {}

    Value run() {
        skip();
        Value result = value(0);
        skip();
        if (position_ != source_.size()) fail("unexpected trailing data");
        return result;
    }

private:
    [[noreturn]] void fail(const char *message) const { throw Error(message, position_); }
    void skip() { while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\n' || source_[position_] == '\r' || source_[position_] == '\t')) ++position_; }
    bool take(char c) { if (position_ < source_.size() && source_[position_] == c) { ++position_; return true; } return false; }
    void literal(std::string_view text) { if (source_.substr(position_, text.size()) != text) fail("invalid literal"); position_ += text.size(); }

    static void appendUtf8(std::string &out, unsigned value) {
        if (value <= 0x7f) out.push_back(static_cast<char>(value));
        else if (value <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (value >> 6))); out.push_back(static_cast<char>(0x80 | (value & 0x3f))); }
        else { out.push_back(static_cast<char>(0xe0 | (value >> 12))); out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (value & 0x3f))); }
    }

    std::string string() {
        if (!take('"')) fail("expected string");
        std::string result;
        while (position_ < source_.size()) {
            const unsigned char c = static_cast<unsigned char>(source_[position_++]);
            if (c == '"') return result;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
            if (position_ >= source_.size()) fail("incomplete escape");
            const char escape = source_[position_++];
            switch (escape) {
            case '"': case '\\': case '/': result.push_back(escape); break;
            case 'b': result.push_back('\b'); break; case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break; case 'r': result.push_back('\r'); break; case 't': result.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > source_.size()) fail("incomplete unicode escape");
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = source_[position_++];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') code += unsigned(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') code += unsigned(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code += unsigned(hex - 'A' + 10);
                    else fail("invalid unicode escape");
                }
                appendUtf8(result, code);
                break;
            }
            default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    Value number() {
        const std::size_t start = position_;
        if (take('-')) {}
        if (take('0')) {}
        else { if (position_ >= source_.size() || source_[position_] < '1' || source_[position_] > '9') fail("invalid number"); while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_; }
        if (take('.')) { if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') fail("invalid fraction"); while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_; }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) { ++position_; if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) ++position_; if (position_ >= source_.size() || source_[position_] < '0' || source_[position_] > '9') fail("invalid exponent"); while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_; }
        double result = 0.0;
        const auto text = source_.substr(start, position_ - start);
        const auto converted = std::from_chars(text.data(), text.data() + text.size(), result);
        if (converted.ec != std::errc{} || !std::isfinite(result)) fail("invalid number");
        return Value(result);
    }

    Value array(std::size_t depth) {
        take('['); skip(); Value::Array result;
        if (take(']')) return Value(std::move(result));
        for (;;) { result.push_back(value(depth)); skip(); if (take(']')) return Value(std::move(result)); if (!take(',')) fail("expected comma"); skip(); }
    }

    Value object(std::size_t depth) {
        take('{'); skip(); Value::Object result;
        if (take('}')) return Value(std::move(result));
        for (;;) { const std::string key = string(); skip(); if (!take(':')) fail("expected colon"); skip(); result.insert_or_assign(key, value(depth)); skip(); if (take('}')) return Value(std::move(result)); if (!take(',')) fail("expected comma"); skip(); }
    }

    Value value(std::size_t depth) {
        if (depth >= maximumDepth_) fail("maximum nesting exceeded");
        skip(); if (position_ >= source_.size()) fail("expected value");
        switch (source_[position_]) {
        case 'n': literal("null"); return Value();
        case 't': literal("true"); return Value(true);
        case 'f': literal("false"); return Value(false);
        case '"': return Value(string());
        case '[': return array(depth + 1);
        case '{': return object(depth + 1);
        default: return number();
        }
    }

    std::string_view source_;
    std::size_t maximumDepth_;
    std::size_t position_ = 0;
};
}

Error::Error(std::string message, std::size_t offset) : std::runtime_error(std::move(message)), offset_(offset) {}
bool Value::boolean(bool fallback) const { if (const auto value = std::get_if<bool>(&value_)) return *value; return fallback; }
double Value::number(double fallback) const { if (const auto value = std::get_if<double>(&value_)) return *value; return fallback; }
std::string_view Value::string(std::string_view fallback) const { if (const auto value = std::get_if<std::string>(&value_)) return *value; return fallback; }
const Value::Array &Value::array() const { if (const auto value = std::get_if<Array>(&value_)) return *value; return emptyArray; }
const Value::Object &Value::object() const { if (const auto value = std::get_if<Object>(&value_)) return *value; return emptyObject; }
const Value &Value::at(std::string_view key) const { const auto &source = object(); const auto found = source.find(key); return found == source.end() ? nullValue : found->second; }
const Value &Value::at(std::size_t index) const { const auto &source = array(); return index < source.size() ? source[index] : nullValue; }
std::size_t Value::size() const { return isArray() ? array().size() : isObject() ? object().size() : 0; }
Value parse(std::string_view source, std::size_t maximumDepth) { return Parser(source, maximumDepth).run(); }

} // namespace rc::json
