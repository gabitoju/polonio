#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace polonio {

class Env;
class Stmt;

struct FunctionValue {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::shared_ptr<Stmt>> body;
    std::shared_ptr<Env> closure;

    bool operator==(const FunctionValue& other) const {
        return name == other.name && params == other.params && body == other.body && closure == other.closure;
    }
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
