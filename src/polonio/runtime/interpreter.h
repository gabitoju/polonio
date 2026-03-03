#pragma once

#include <exception>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "polonio/parser/ast.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/output.h"

namespace polonio {

struct CGIContext;
struct SessionContext;

class ReturnSignal : public std::exception {
public:
    explicit ReturnSignal(Value value) : value_(std::move(value)) {}
    const Value& value() const { return value_; }
    const char* what() const noexcept override { return "return"; }

private:
    Value value_;
};

struct ResponseContext {
    int status_code = 200;
    bool headers_sent = false;
    std::vector<std::pair<std::string, std::string>> headers;

    void set_status(int code) { status_code = code; }
    void add_header(const std::string& name, const std::string& value);
    bool has_content_type() const;
    void ensure_default_headers();
    void emit(std::ostream& os);
};

class Interpreter {
public:
    explicit Interpreter(std::shared_ptr<Env> env = std::make_shared<Env>(), std::string path = {});

    Value eval_expr(const ExprPtr& expr);
    void exec_stmt(const StmtPtr& stmt);
    void exec_program(const Program& program);

    const std::string& output() const { return output_.str(); }
    std::shared_ptr<Env> env() const { return env_; }
    const std::string& path() const { return path_; }
    void write_text(const std::string& text);
    void clear_output();
    using IncludeCallback = std::function<void(const std::string&, const Location&)>;
    void set_include_callback(IncludeCallback cb) { include_callback_ = std::move(cb); }
    void set_response_context(ResponseContext* ctx) { response_context_ = ctx; }
    ResponseContext* response_context() const { return response_context_; }
    void set_cgi_context(CGIContext* ctx) { cgi_context_ = ctx; }
    CGIContext* cgi_context() const { return cgi_context_; }
    void set_session_context(SessionContext* ctx) { session_context_ = ctx; }
    SessionContext* session_context() const { return session_context_; }

private:
    Value eval_expr_internal(const ExprPtr& expr);
    Value eval_literal(const LiteralExpr& literal);
    Value eval_identifier(const IdentifierExpr& ident);
    Value eval_unary(const UnaryExpr& unary);
    Value eval_binary(const BinaryExpr& binary);
    Value eval_assignment(const AssignmentExpr& assignment);
    Value eval_call(const CallExpr& call);
    Value eval_index(const IndexExpr& index);
    Value eval_array(const ArrayLiteralExpr& array);
    Value eval_object(const ObjectLiteralExpr& object);

    void exec_var(const VarDeclStmt& stmt);
    void exec_echo(const EchoStmt& stmt);
    void exec_expr_stmt(const ExprStmt& stmt);
    void exec_return(const ReturnStmt& stmt);
    void exec_function(const FunctionStmt& stmt);
    void exec_if(const IfStmt& stmt);
    void exec_while(const WhileStmt& stmt);
    void exec_for(const ForStmt& stmt);
    void exec_block(const std::vector<StmtPtr>& statements);

    [[noreturn]] void runtime_error(const std::string& message);
    Value lookup_identifier(const std::string& name);
    double require_number(const Value& value, const std::string& context);

    static std::string decode_string(const std::string& literal);
    std::string stringify_for_concat(const Value& value) const;

    std::shared_ptr<Env> env_;
    OutputBuffer output_;
    std::string path_;
    int call_depth_ = 0;
    IncludeCallback include_callback_;
    ResponseContext* response_context_ = nullptr;
    CGIContext* cgi_context_ = nullptr;
    SessionContext* session_context_ = nullptr;
};

} // namespace polonio
