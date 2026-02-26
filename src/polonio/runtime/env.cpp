#include "polonio/runtime/env.h"

#include <utility>

namespace polonio {

Env::Env(std::shared_ptr<Env> parent) : parent_(std::move(parent)) {}

std::shared_ptr<Env> Env::parent() const { return parent_; }

void Env::set_local(const std::string& name, Value value) {
    values_[name] = std::move(value);
}

bool Env::has_local(const std::string& name) const {
    return values_.find(name) != values_.end();
}

Value* Env::find(const std::string& name) {
    auto it = values_.find(name);
    if (it != values_.end()) {
        return &it->second;
    }
    if (parent_) {
        return parent_->find(name);
    }
    return nullptr;
}

const Value* Env::find(const std::string& name) const {
    auto it = values_.find(name);
    if (it != values_.end()) {
        return &it->second;
    }
    if (parent_) {
        const Env* parent = parent_.get();
        return parent->find(name);
    }
    return nullptr;
}

void Env::assign(const std::string& name, Value value) {
    if (auto* existing = find(name)) {
        *existing = std::move(value);
        return;
    }
    set_local(name, std::move(value));
}

} // namespace polonio
