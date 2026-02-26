#include "polonio/runtime/interpreter.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "polonio/common/error.h"

namespace polonio {

namespace {

bool is_integer(double value) {
    return std::floor(value) == value;
}

} // namespace

Interpreter::Interpreter(std::shared_ptr<Env> env, std::string path)
    : env_(env ? std::move(env) : std::make_shared<Env>()), path_(std::move(path)) {}

Value Interpreter::eval_expr(const ExprPtr& expr) { return eval_expr_internal(expr); }

void Interpreter::exec_stmt(const StmtPtr& stmt) {
    if (auto var = std::dynamic_pointer_cast<VarDeclStmt>(stmt)) {
        exec_var(*var);
        return;
    }
    if (auto echo = std::dynamic_pointer_cast<EchoStmt>(stmt)) {
        exec_echo(*echo);
        return;
    }
    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStmt>(stmt)) {
        exec_expr_stmt(*expr_stmt);
        return;
    }
    if (auto ret = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        exec_return(*ret);
        return;
    }
    if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
        exec_function(*fn);
        return;
    }
    if (auto if_stmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        exec_if(*if_stmt);
        return;
    }
    runtime_error("statement type not supported yet");
}

void Interpreter::exec_program(const Program& program) {
    for (const auto& stmt : program.statements()) {
        exec_stmt(stmt);
    }
}

Value Interpreter::eval_expr_internal(const ExprPtr& expr) {
    if (auto literal = std::dynamic_pointer_cast<LiteralExpr>(expr)) {
        return eval_literal(*literal);
    }
    if (auto ident = std::dynamic_pointer_cast<IdentifierExpr>(expr)) {
        return eval_identifier(*ident);
    }
    if (auto unary = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
        return eval_unary(*unary);
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        return eval_binary(*binary);
    }
    if (auto assignment = std::dynamic_pointer_cast<AssignmentExpr>(expr)) {
        return eval_assignment(*assignment);
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
        return eval_call(*call);
    }
    if (auto index = std::dynamic_pointer_cast<IndexExpr>(expr)) {
        return eval_index(*index);
    }
    if (auto array = std::dynamic_pointer_cast<ArrayLiteralExpr>(expr)) {
        return eval_array(*array);
    }
    if (auto object = std::dynamic_pointer_cast<ObjectLiteralExpr>(expr)) {
        return eval_object(*object);
    }
    runtime_error("expression type not supported yet");
}

Value Interpreter::eval_literal(const LiteralExpr& literal) {
    const std::string& repr = literal.repr();
    if (repr == "null") {
        return Value();
    }
    if (repr == "bool(true)") {
        return Value(true);
    }
    if (repr == "bool(false)") {
        return Value(false);
    }
    if (repr.rfind("num(", 0) == 0 && repr.back() == ')') {
        std::string inner = repr.substr(4, repr.size() - 5);
        return Value(std::stod(inner));
    }
    if (repr.rfind("str(", 0) == 0 && repr.back() == ')') {
        std::string inner = repr.substr(4, repr.size() - 5);
        return Value(decode_string(inner));
    }
    runtime_error("unknown literal: " + repr);
}

Value Interpreter::eval_identifier(const IdentifierExpr& ident) {
    return lookup_identifier(ident.name());
}

Value Interpreter::eval_unary(const UnaryExpr& unary) {
    Value right = eval_expr_internal(unary.right());
    if (unary.op() == "-") {
        double number = require_number(right, "unary '-'");
        return Value(-number);
    }
    if (unary.op() == "not") {
        return Value(!right.is_truthy());
    }
    runtime_error("unsupported unary operator: " + unary.op());
}

Value Interpreter::eval_binary(const BinaryExpr& binary) {
    const std::string& op = binary.op();
    if (op == "and") {
        Value left = eval_expr_internal(binary.left());
        if (!left.is_truthy()) {
            return Value(false);
        }
        Value right = eval_expr_internal(binary.right());
        return Value(right.is_truthy());
    }
    if (op == "or") {
        Value left = eval_expr_internal(binary.left());
        if (left.is_truthy()) {
            return Value(true);
        }
        Value right = eval_expr_internal(binary.right());
        return Value(right.is_truthy());
    }

    Value left = eval_expr_internal(binary.left());
    Value right = eval_expr_internal(binary.right());

    if (op == "+") {
        return Value(require_number(left, "+") + require_number(right, "+"));
    }
    if (op == "-") {
        return Value(require_number(left, "-") - require_number(right, "-"));
    }
    if (op == "*") {
        return Value(require_number(left, "*") * require_number(right, "*"));
    }
    if (op == "/") {
        double divisor = require_number(right, "/");
        if (divisor == 0.0) {
            runtime_error("division by zero");
        }
        return Value(require_number(left, "/") / divisor);
    }
    if (op == "%") {
        double lhs = require_number(left, "%");
        double rhs = require_number(right, "%");
        if (rhs == 0.0) {
            runtime_error("division by zero");
        }
        return Value(std::fmod(lhs, rhs));
    }
    if (op == "..") {
        std::string lhs = stringify_for_concat(left);
        std::string rhs = stringify_for_concat(right);
        return Value(lhs + rhs);
    }
    if (op == "==") {
        return Value(left == right);
    }
    if (op == "!=") {
        return Value(left != right);
    }
    if (op == "<") {
        return Value(require_number(left, "<") < require_number(right, "<"));
    }
    if (op == "<=") {
        return Value(require_number(left, "<=") <= require_number(right, "<="));
    }
    if (op == ">") {
        return Value(require_number(left, ">") > require_number(right, ">"));
    }
    if (op == ">=") {
        return Value(require_number(left, ">=") >= require_number(right, ">="));
    }

    runtime_error("unsupported binary operator: " + op);
}

Value Interpreter::eval_assignment(const AssignmentExpr& assignment) {
    if (std::dynamic_pointer_cast<IndexExpr>(assignment.target())) {
        runtime_error("index assignment not supported yet");
    }
    auto ident = std::dynamic_pointer_cast<IdentifierExpr>(assignment.target());
    if (!ident) {
        runtime_error("assignment target must be an identifier");
    }
    const std::string& name = ident->name();
    Value rhs = eval_expr_internal(assignment.value());
    const std::string& op = assignment.op();

    if (op == "=") {
        env_->assign(name, rhs);
        return rhs;
    }

    Value current = lookup_identifier(name);
    if (op == "+=") {
        double result = require_number(current, "+=") + require_number(rhs, "+=");
        Value updated(result);
        env_->assign(name, updated);
        return updated;
    }
    if (op == "-=") {
        double result = require_number(current, "-=") - require_number(rhs, "-=");
        Value updated(result);
        env_->assign(name, updated);
        return updated;
    }
    if (op == "*=") {
        double result = require_number(current, "*=") * require_number(rhs, "*=");
        Value updated(result);
        env_->assign(name, updated);
        return updated;
    }
    if (op == "/=") {
        double divisor = require_number(rhs, "/=");
        if (divisor == 0.0) {
            runtime_error("division by zero");
        }
        double result = require_number(current, "/=") / divisor;
        Value updated(result);
        env_->assign(name, updated);
        return updated;
    }
    if (op == "%=") {
        double rhs_number = require_number(rhs, "%=");
        if (rhs_number == 0.0) {
            runtime_error("division by zero");
        }
        double lhs_number = require_number(current, "%=");
        Value updated(std::fmod(lhs_number, rhs_number));
        env_->assign(name, updated);
        return updated;
    }
    if (op == "..=") {
        std::string lhs = stringify_for_concat(current);
        std::string rhs_str = stringify_for_concat(rhs);
        Value updated(lhs + rhs_str);
        env_->assign(name, updated);
        return updated;
    }

    runtime_error("unsupported assignment operator: " + op);
}

Value Interpreter::eval_call(const CallExpr& call) {
    Value callee = eval_expr_internal(call.callee());
    if (!std::holds_alternative<FunctionValue>(callee.storage())) {
        runtime_error("attempt to call non-function value");
    }
    const auto& function = std::get<FunctionValue>(callee.storage());

    std::vector<Value> args;
    args.reserve(call.args().size());
    for (const auto& arg_expr : call.args()) {
        args.push_back(eval_expr_internal(arg_expr));
    }

    auto closure_env = function.closure ? function.closure : std::make_shared<Env>();
    auto call_env = std::make_shared<Env>(closure_env);
    for (std::size_t i = 0; i < function.params.size(); ++i) {
        Value arg_value = i < args.size() ? args[i] : Value();
        call_env->set_local(function.params[i], arg_value);
    }
    if (!function.name.empty()) {
        call_env->set_local(function.name, callee);
    }

    auto previous_env = env_;
    env_ = call_env;
    call_depth_ += 1;
    try {
        exec_block(function.body);
        env_ = previous_env;
        call_depth_ -= 1;
        return Value();
    } catch (const ReturnSignal& signal) {
        env_ = previous_env;
        call_depth_ -= 1;
        return signal.value();
    } catch (...) {
        env_ = previous_env;
        call_depth_ -= 1;
        throw;
    }
}

Value Interpreter::eval_index(const IndexExpr& index) {
    Value collection = eval_expr_internal(index.object());
    Value idx = eval_expr_internal(index.index());

    if (std::holds_alternative<Value::Array>(collection.storage())) {
        double numeric = require_number(idx, "array index");
        if (!is_integer(numeric) || numeric < 0) {
            runtime_error("array index must be a non-negative integer");
        }
        const auto& array = std::get<Value::Array>(collection.storage());
        std::size_t i = static_cast<std::size_t>(numeric);
        if (i >= array.size()) {
            runtime_error("array index out of range");
        }
        return array[i];
    }

    if (std::holds_alternative<Value::Object>(collection.storage())) {
        if (!std::holds_alternative<std::string>(idx.storage())) {
            runtime_error("object keys must be strings");
        }
        const auto& key = std::get<std::string>(idx.storage());
        const auto& object = std::get<Value::Object>(collection.storage());
        auto it = object.find(key);
        if (it == object.end()) {
            return Value();
        }
        return it->second;
    }

    runtime_error("indexing only supported on arrays and objects for now");
}

Value Interpreter::eval_array(const ArrayLiteralExpr& array) {
    Value::Array values;
    values.reserve(array.elements().size());
    for (const auto& element : array.elements()) {
        values.push_back(eval_expr_internal(element));
    }
    return Value(std::move(values));
}

Value Interpreter::eval_object(const ObjectLiteralExpr& object) {
    Value::Object map;
    for (const auto& field : object.fields()) {
        map[decode_string(field.first)] = eval_expr_internal(field.second);
    }
    return Value(std::move(map));
}

void Interpreter::exec_var(const VarDeclStmt& stmt) {
    Value value;
    if (stmt.has_initializer()) {
        value = eval_expr_internal(stmt.initializer());
    }
    env_->set_local(stmt.name(), value);
}

void Interpreter::exec_echo(const EchoStmt& stmt) {
    Value value = eval_expr_internal(stmt.expr());
    output_.write(value);
}

void Interpreter::exec_expr_stmt(const ExprStmt& stmt) { (void)eval_expr_internal(stmt.expr()); }

void Interpreter::exec_return(const ReturnStmt& stmt) {
    if (call_depth_ == 0) {
        runtime_error("return outside of function");
    }
    Value value;
    if (stmt.has_value()) {
        value = eval_expr_internal(stmt.value());
    }
    throw ReturnSignal(std::move(value));
}

void Interpreter::exec_function(const FunctionStmt& stmt) {
    FunctionValue fn_value;
    fn_value.name = stmt.name();
    fn_value.params = stmt.params();
    fn_value.body = stmt.body();
    fn_value.closure = env_;
    env_->set_local(stmt.name(), Value(fn_value));
}

void Interpreter::exec_if(const IfStmt& stmt) {
    for (const auto& branch : stmt.branches()) {
        Value condition = eval_expr_internal(branch.condition);
        if (condition.is_truthy()) {
            exec_block(branch.body);
            return;
        }
    }
    if (!stmt.else_body().empty()) {
        exec_block(stmt.else_body());
    }
}

void Interpreter::exec_block(const std::vector<StmtPtr>& statements) {
    for (const auto& stmt : statements) {
        exec_stmt(stmt);
    }
}

[[noreturn]] void Interpreter::runtime_error(const std::string& message) {
    throw PolonioError(ErrorKind::Runtime, message, path_, Location::start());
}

Value Interpreter::lookup_identifier(const std::string& name) {
    if (auto* value = env_->find(name)) {
        return *value;
    }
    runtime_error("undefined variable: " + name);
}

double Interpreter::require_number(const Value& value, const std::string& context) {
    if (!std::holds_alternative<double>(value.storage())) {
        runtime_error(context + " expects numbers");
    }
    return std::get<double>(value.storage());
}

std::string Interpreter::decode_string(const std::string& literal) {
    if (literal.size() < 2) {
        return {};
    }
    std::string result;
    for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
        char c = literal[i];
        if (c == '\\' && i + 1 < literal.size()) {
            char next = literal[++i];
            switch (next) {
            case 'n':
                result.push_back('\n');
                break;
            case 't':
                result.push_back('\t');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '"':
                result.push_back('"');
                break;
            case '\'':
                result.push_back('\'');
                break;
            default:
                result.push_back(next);
                break;
            }
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::string Interpreter::stringify_for_concat(const Value& value) const {
    return OutputBuffer::value_to_string(value);
}

} // namespace polonio
