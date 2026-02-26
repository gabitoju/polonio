#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "polonio/runtime/value.h"

namespace polonio {

class Env {
public:
    explicit Env(std::shared_ptr<Env> parent = nullptr);

    std::shared_ptr<Env> parent() const;

    void set_local(const std::string& name, Value value);
    bool has_local(const std::string& name) const;

    Value* find(const std::string& name);
    const Value* find(const std::string& name) const;

    void assign(const std::string& name, Value value);

private:
    std::shared_ptr<Env> parent_;
    std::unordered_map<std::string, Value> values_;
};

} // namespace polonio
