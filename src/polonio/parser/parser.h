#pragma once

#include <string>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/lexer/lexer.h"
#include "polonio/parser/ast.h"

namespace polonio {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string path = {});

    ExprPtr parse_expression();
    Program parse_program();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool match(TokenKind kind);
    bool match(std::initializer_list<TokenKind> kinds);
    bool check(TokenKind kind) const;
    const Token& advance();
    bool is_at_end() const;
    [[noreturn]] void error(const Token& token, const std::string& message);
    const Token& consume(TokenKind kind, const std::string& message);

    StmtPtr declaration();
    StmtPtr statement();
    StmtPtr var_declaration();
    StmtPtr echo_statement();
    StmtPtr if_statement();
    std::vector<StmtPtr> block_until(std::initializer_list<TokenKind> terminators);
    StmtPtr expression_statement();

    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr or_expr();
    ExprPtr and_expr();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr concat();
    ExprPtr addition();
    ExprPtr multiplication();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    ExprPtr array_literal();
    ExprPtr object_literal();

    std::string literal_repr(const Token& token) const;

    std::vector<Token> tokens_;
    std::string path_;
    std::size_t current_ = 0;
};

} // namespace polonio
