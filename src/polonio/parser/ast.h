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

class CallExpr : public Expr {
public:
    CallExpr(ExprPtr callee, std::vector<ExprPtr> args)
        : callee_(std::move(callee)), args_(std::move(args)) {}

    std::string dump() const override {
        std::string out = "call(" + callee_->dump();
        for (const auto& arg : args_) {
            out += ", " + arg->dump();
        }
        out += ')';
        return out;
    }

private:
    ExprPtr callee_;
    std::vector<ExprPtr> args_;
};

class IndexExpr : public Expr {
public:
    IndexExpr(ExprPtr object, ExprPtr index)
        : object_(std::move(object)), index_(std::move(index)) {}

    std::string dump() const override {
        return "index(" + object_->dump() + ", " + index_->dump() + ")";
    }

    const ExprPtr& object() const { return object_; }

private:
    ExprPtr object_;
    ExprPtr index_;
};

class AssignmentExpr : public Expr {
public:
    AssignmentExpr(ExprPtr target, std::string op, ExprPtr value)
        : target_(std::move(target)), op_(std::move(op)), value_(std::move(value)) {}

    std::string dump() const override {
        return "assign(" + target_->dump() + ", " + op_ + ", " + value_->dump() + ")";
    }

private:
    ExprPtr target_;
    std::string op_;
    ExprPtr value_;
};

class Stmt {
public:
    virtual ~Stmt() = default;
    virtual std::string dump() const = 0;
};

using StmtPtr = std::shared_ptr<Stmt>;

class VarDeclStmt : public Stmt {
public:
    VarDeclStmt(std::string name, ExprPtr initializer)
        : name_(std::move(name)), initializer_(std::move(initializer)) {}

    std::string dump() const override {
        if (initializer_) {
            return "Var(" + name_ + ", " + initializer_->dump() + ")";
        }
        return "Var(" + name_ + ")";
    }

private:
    std::string name_;
    ExprPtr initializer_;
};

class EchoStmt : public Stmt {
public:
    explicit EchoStmt(ExprPtr expr) : expr_(std::move(expr)) {}

    std::string dump() const override { return "Echo(" + expr_->dump() + ")"; }

private:
    ExprPtr expr_;
};

class ExprStmt : public Stmt {
public:
    explicit ExprStmt(ExprPtr expr) : expr_(std::move(expr)) {}

    std::string dump() const override { return "Expr(" + expr_->dump() + ")"; }

private:
    ExprPtr expr_;
};

class Program {
public:
    explicit Program(std::vector<StmtPtr> statements)
        : statements_(std::move(statements)) {}

    const std::vector<StmtPtr>& statements() const { return statements_; }

    std::string dump() const {
        std::string out = "Program(";
        for (std::size_t i = 0; i < statements_.size(); ++i) {
            if (i > 0) out += ", ";
            out += statements_[i]->dump();
        }
        out += ')';
        return out;
    }

private:
    std::vector<StmtPtr> statements_;
};

} // namespace polonio
