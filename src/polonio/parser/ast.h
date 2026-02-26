#pragma once

#include <memory>
#include <string>

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

} // namespace polonio
