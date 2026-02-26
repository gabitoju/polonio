#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace polonio {

struct FunctionValue {
    std::size_t id = 0;
    bool operator==(const FunctionValue& other) const { return id == other.id; }
};

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::unordered_map<std::string, Value>;
    using Storage = std::variant<std::monostate, bool, double, std::string, Array, Object, FunctionValue>;

    Value();
    Value(std::nullptr_t);
    explicit Value(bool b);
    explicit Value(double d);
    explicit Value(int i);
    Value(const std::string& s);
    Value(std::string&& s);
    Value(const char* s);
    explicit Value(const Array& array);
    explicit Value(Array&& array);
    explicit Value(const Object& object);
    explicit Value(Object&& object);
    explicit Value(FunctionValue fn);

    std::string type_name() const;
    bool is_truthy() const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

    const Storage& storage() const;
    Storage& storage();

private:
    Storage storage_;
};

} // namespace polonio
