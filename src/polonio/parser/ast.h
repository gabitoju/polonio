#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace polonio {

class Expr {
public:
    virtual ~Expr() = default;
    virtual std::string dump() const = 0;
};

using ExprPtr = std::shared_ptr<Expr>;

class LiteralExpr : public Expr {
public:
    explicit LiteralExpr(std::string repr) : repr_(std::move(repr)) {}
    std::string dump() const override { return repr_; }

private:
    std::string repr_;
};

class IdentifierExpr : public Expr {
public:
    explicit IdentifierExpr(std::string name) : name_(std::move(name)) {}
    std::string dump() const override { return "ident(" + name_ + ")"; }

private:
    std::string name_;
};

class UnaryExpr : public Expr {
public:
    UnaryExpr(std::string op, ExprPtr right)
        : op_(std::move(op)), right_(std::move(right)) {}

    std::string dump() const override {
        return "(" + op_ + " " + right_->dump() + ")";
    }

private:
    std::string op_;
    ExprPtr right_;
};

class BinaryExpr : public Expr {
public:
    BinaryExpr(std::string op, ExprPtr left, ExprPtr right)
        : op_(std::move(op)), left_(std::move(left)), right_(std::move(right)) {}

    std::string dump() const override {
        return "(" + op_ + " " + left_->dump() + " " + right_->dump() + ")";
    }

private:
    std::string op_;
    ExprPtr left_;
    ExprPtr right_;
};

class ArrayLiteralExpr : public Expr {
public:
    explicit ArrayLiteralExpr(std::vector<ExprPtr> elements)
        : elements_(std::move(elements)) {}

    std::string dump() const override {
        std::string out = "array(";
        for (std::size_t i = 0; i < elements_.size(); ++i) {
            if (i > 0) out += ", ";
            out += elements_[i]->dump();
        }
        out += ')';
        return out;
    }

private:
    std::vector<ExprPtr> elements_;
};

class ObjectLiteralExpr : public Expr {
public:
    explicit ObjectLiteralExpr(std::vector<std::pair<std::string, ExprPtr>> fields)
        : fields_(std::move(fields)) {}

    std::string dump() const override {
        std::string out = "object(";
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (i > 0) out += ", ";
            out += fields_[i].first + ": " + fields_[i].second->dump();
        }
        out += ')';
        return out;
    }

private:
    std::vector<std::pair<std::string, ExprPtr>> fields_;
};

} // namespace polonio
