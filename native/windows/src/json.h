#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace rc::json {

class Error final : public std::runtime_error {
public:
    Error(std::string message, std::size_t offset);
    std::size_t offset() const noexcept { return offset_; }
private:
    std::size_t offset_;
};

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value() : value_(nullptr) {}
    explicit Value(Storage value) : value_(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool isBool() const { return std::holds_alternative<bool>(value_); }
    bool isNumber() const { return std::holds_alternative<double>(value_); }
    bool isString() const { return std::holds_alternative<std::string>(value_); }
    bool isArray() const { return std::holds_alternative<Array>(value_); }
    bool isObject() const { return std::holds_alternative<Object>(value_); }

    bool boolean(bool fallback = false) const;
    double number(double fallback = 0.0) const;
    std::string_view string(std::string_view fallback = {}) const;
    const Array &array() const;
    const Object &object() const;
    const Value &at(std::string_view key) const;
    const Value &at(std::size_t index) const;
    std::size_t size() const;

private:
    Storage value_;
};

Value parse(std::string_view source, std::size_t maximumDepth = 64);

} // namespace rc::json
