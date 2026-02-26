#pragma once

#include <memory>
#include <optional>
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
    const std::string& repr() const { return repr_; }

private:
    std::string repr_;
};

class IdentifierExpr : public Expr {
public:
    explicit IdentifierExpr(std::string name) : name_(std::move(name)) {}
    std::string dump() const override { return "ident(" + name_ + ")"; }
    const std::string& name() const { return name_; }

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
    const std::string& op() const { return op_; }
    const ExprPtr& right() const { return right_; }

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
    const std::string& op() const { return op_; }
    const ExprPtr& left() const { return left_; }
    const ExprPtr& right() const { return right_; }

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
    const std::vector<ExprPtr>& elements() const { return elements_; }

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
    const std::vector<std::pair<std::string, ExprPtr>>& fields() const { return fields_; }

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
    const ExprPtr& callee() const { return callee_; }
    const std::vector<ExprPtr>& args() const { return args_; }

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
    const ExprPtr& index() const { return index_; }

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
    const ExprPtr& target() const { return target_; }
    const std::string& op() const { return op_; }
    const ExprPtr& value() const { return value_; }

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
    const std::string& name() const { return name_; }
    const ExprPtr& initializer() const { return initializer_; }
    bool has_initializer() const { return static_cast<bool>(initializer_); }

private:
    std::string name_;
    ExprPtr initializer_;
};

class EchoStmt : public Stmt {
public:
    explicit EchoStmt(ExprPtr expr) : expr_(std::move(expr)) {}

    std::string dump() const override { return "Echo(" + expr_->dump() + ")"; }
    const ExprPtr& expr() const { return expr_; }

private:
    ExprPtr expr_;
};

class ExprStmt : public Stmt {
public:
    explicit ExprStmt(ExprPtr expr) : expr_(std::move(expr)) {}

    std::string dump() const override { return "Expr(" + expr_->dump() + ")"; }
    const ExprPtr& expr() const { return expr_; }

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

struct IfBranch {
    ExprPtr condition;
    std::vector<StmtPtr> body;
};

class IfStmt : public Stmt {
public:
    IfStmt(std::vector<IfBranch> branches, std::vector<StmtPtr> else_body)
        : branches_(std::move(branches)), else_body_(std::move(else_body)) {}

    std::string dump() const override {
        std::string out = "If(";
        for (std::size_t i = 0; i < branches_.size(); ++i) {
            if (i > 0) out += ", ";
            out += "Branch(" + branches_[i].condition->dump() + ", [";
            for (std::size_t j = 0; j < branches_[i].body.size(); ++j) {
                if (j > 0) out += ", ";
                out += branches_[i].body[j]->dump();
            }
            out += "])";
        }
        if (!else_body_.empty()) {
            out += (branches_.empty() ? "" : ", ") + std::string("Else([");
            for (std::size_t i = 0; i < else_body_.size(); ++i) {
                if (i > 0) out += ", ";
                out += else_body_[i]->dump();
            }
            out += "])";
        }
        out += ')';
        return out;
    }
    const std::vector<IfBranch>& branches() const { return branches_; }
    const std::vector<StmtPtr>& else_body() const { return else_body_; }

private:
    std::vector<IfBranch> branches_;
    std::vector<StmtPtr> else_body_;
};

class WhileStmt : public Stmt {
public:
    WhileStmt(ExprPtr condition, std::vector<StmtPtr> body)
        : condition_(std::move(condition)), body_(std::move(body)) {}

    std::string dump() const override {
        std::string out = "While(" + condition_->dump() + ", [";
        for (std::size_t i = 0; i < body_.size(); ++i) {
            if (i > 0) out += ", ";
            out += body_[i]->dump();
        }
        out += "])";
        return out;
    }

private:
    ExprPtr condition_;
    std::vector<StmtPtr> body_;
};

class ForStmt : public Stmt {
public:
    ForStmt(std::optional<std::string> index_name,
            std::string value_name,
            ExprPtr iterable,
            std::vector<StmtPtr> body)
        : index_name_(std::move(index_name)),
          value_name_(std::move(value_name)),
          iterable_(std::move(iterable)),
          body_(std::move(body)) {}

    std::string dump() const override {
        std::string out = "For(";
        if (index_name_) {
            out += *index_name_ + ", " + value_name_ + ", " + iterable_->dump() + ", [";
        } else {
            out += value_name_ + ", " + iterable_->dump() + ", [";
        }
        for (std::size_t i = 0; i < body_.size(); ++i) {
            if (i > 0) out += ", ";
            out += body_[i]->dump();
        }
        out += "])";
        return out;
    }

private:
    std::optional<std::string> index_name_;
    std::string value_name_;
    ExprPtr iterable_;
    std::vector<StmtPtr> body_;
};

class ReturnStmt : public Stmt {
public:
    explicit ReturnStmt(ExprPtr value) : value_(std::move(value)) {}

    std::string dump() const override {
        if (value_) {
            return "Return(" + value_->dump() + ")";
        }
        return "Return()";
    }
    const ExprPtr& value() const { return value_; }
    bool has_value() const { return static_cast<bool>(value_); }

private:
    ExprPtr value_;
};

class FunctionStmt : public Stmt {
public:
    FunctionStmt(std::string name,
                 std::vector<std::string> params,
                 std::vector<StmtPtr> body)
        : name_(std::move(name)),
          params_(std::move(params)),
          body_(std::move(body)) {}

    std::string dump() const override {
        std::string out = "Function(" + name_ + ", [";
        for (std::size_t i = 0; i < params_.size(); ++i) {
            if (i > 0) out += ", ";
            out += params_[i];
        }
        out += "], [";
        for (std::size_t i = 0; i < body_.size(); ++i) {
            if (i > 0) out += ", ";
            out += body_[i]->dump();
        }
        out += "])";
        return out;
    }
    const std::string& name() const { return name_; }
    const std::vector<std::string>& params() const { return params_; }
    const std::vector<StmtPtr>& body() const { return body_; }

private:
    std::string name_;
    std::vector<std::string> params_;
    std::vector<StmtPtr> body_;
};

} // namespace polonio
