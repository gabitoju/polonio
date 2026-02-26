#include "polonio/parser/parser.h"

#include <initializer_list>
#include <utility>

namespace polonio {

Parser::Parser(std::vector<Token> tokens, std::string path)
    : tokens_(std::move(tokens)), path_(std::move(path)) {}

ExprPtr Parser::parse_expression() {
    auto expr = assignment();
    if (!is_at_end()) {
        error(peek(), "unexpected token after expression");
    }
    return expr;
}

Program Parser::parse_program() {
    std::vector<StmtPtr> statements;
    while (!is_at_end()) {
        statements.push_back(declaration());
        match(TokenKind::Semicolon);
    }
    return Program(std::move(statements));
}

ExprPtr Parser::expression() { return or_expr(); }

ExprPtr Parser::assignment() {
    auto expr = or_expr();

    if (match({TokenKind::Equal, TokenKind::PlusEqual, TokenKind::MinusEqual,
               TokenKind::StarEqual, TokenKind::SlashEqual, TokenKind::PercentEqual,
               TokenKind::DotDotEqual})) {
        std::string op = previous().lexeme;
        auto value = assignment();

        if (auto ident = std::dynamic_pointer_cast<IdentifierExpr>(expr)) {
            return std::make_shared<AssignmentExpr>(expr, op, value);
        }
        if (auto index = std::dynamic_pointer_cast<IndexExpr>(expr)) {
            return std::make_shared<AssignmentExpr>(expr, op, value);
        }
        error(previous(), "invalid assignment target");
    }

    return expr;
}

ExprPtr Parser::or_expr() {
    auto expr = and_expr();
    while (match(TokenKind::Or)) {
        std::string op = previous().lexeme;
        auto right = and_expr();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::and_expr() {
    auto expr = equality();
    while (match(TokenKind::And)) {
        std::string op = previous().lexeme;
        auto right = equality();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match({TokenKind::EqualEqual, TokenKind::NotEqual})) {
        std::string op = previous().lexeme;
        auto right = comparison();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::comparison() {
    auto expr = concat();
    while (match({TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual})) {
        std::string op = previous().lexeme;
        auto right = concat();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::concat() {
    auto expr = addition();
    while (match(TokenKind::DotDot)) {
        std::string op = previous().lexeme;
        auto right = addition();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::addition() {
    auto expr = multiplication();
    while (match({TokenKind::Plus, TokenKind::Minus})) {
        std::string op = previous().lexeme;
        auto right = multiplication();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::multiplication() {
    auto expr = unary();
    while (match({TokenKind::Star, TokenKind::Slash, TokenKind::Percent})) {
        std::string op = previous().lexeme;
        auto right = unary();
        expr = std::make_shared<BinaryExpr>(op, expr, right);
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match(TokenKind::Not)) {
        std::string op = previous().lexeme;
        auto right = unary();
        return std::make_shared<UnaryExpr>(op, right);
    }
    if (match(TokenKind::Minus)) {
        std::string op = previous().lexeme;
        auto right = unary();
        return std::make_shared<UnaryExpr>(op, right);
    }
    return postfix();
}

ExprPtr Parser::postfix() {
    auto expr = primary();
    while (true) {
        if (match(TokenKind::LeftParen)) {
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RightParen)) {
                do {
                    args.push_back(expression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RightParen, "expected ')' after arguments");
            expr = std::make_shared<CallExpr>(expr, std::move(args));
            continue;
        }
        if (match(TokenKind::LeftBracket)) {
            auto index_expr = expression();
            consume(TokenKind::RightBracket, "expected ']' after index");
            expr = std::make_shared<IndexExpr>(expr, index_expr);
            continue;
        }
        break;
    }
    return expr;
}

ExprPtr Parser::primary() {
    if (match(TokenKind::Number)) {
        return std::make_shared<LiteralExpr>("num(" + previous().lexeme + ")");
    }
    if (match(TokenKind::String)) {
        return std::make_shared<LiteralExpr>("str(" + previous().lexeme + ")");
    }
    if (match(TokenKind::True)) {
        return std::make_shared<LiteralExpr>("bool(true)");
    }
    if (match(TokenKind::False)) {
        return std::make_shared<LiteralExpr>("bool(false)");
    }
    if (match(TokenKind::Null)) {
        return std::make_shared<LiteralExpr>("null");
    }
    if (match(TokenKind::Identifier)) {
        return std::make_shared<IdentifierExpr>(previous().lexeme);
    }
    if (match(TokenKind::LeftParen)) {
        auto expr = expression();
        consume(TokenKind::RightParen, "expected ')' after expression");
        return expr;
    }
    if (match(TokenKind::LeftBracket)) {
        return array_literal();
    }
    if (match(TokenKind::LeftBrace)) {
        return object_literal();
    }

    error(peek(), "expected expression");
}

ExprPtr Parser::array_literal() {
    std::vector<ExprPtr> elements;
    if (!check(TokenKind::RightBracket)) {
        do {
            elements.push_back(expression());
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RightBracket, "expected ']' after array literal");
    return std::make_shared<ArrayLiteralExpr>(std::move(elements));
}

ExprPtr Parser::object_literal() {
    std::vector<std::pair<std::string, ExprPtr>> fields;
    if (!check(TokenKind::RightBrace)) {
        do {
            if (!match(TokenKind::String)) {
                error(peek(), "expected string key in object literal");
            }
            std::string key = previous().lexeme;
            consume(TokenKind::Colon, "expected ':' after object key");
            auto value = expression();
            fields.emplace_back(key, value);
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RightBrace, "expected '}' after object literal");
    return std::make_shared<ObjectLiteralExpr>(std::move(fields));
}

StmtPtr Parser::declaration() {
    if (match(TokenKind::Var)) {
        return var_declaration();
    }
    return statement();
}

StmtPtr Parser::var_declaration() {
    if (!match(TokenKind::Identifier)) {
        error(peek(), "expected identifier after 'var'");
    }
    std::string name = previous().lexeme;
    ExprPtr initializer;
    if (match(TokenKind::Equal)) {
        initializer = assignment();
    }
    return std::make_shared<VarDeclStmt>(name, initializer);
}

StmtPtr Parser::statement() {
    if (match(TokenKind::Echo)) {
        return echo_statement();
    }
    if (match(TokenKind::If)) {
        return if_statement();
    }
    if (match(TokenKind::While)) {
        return while_statement();
    }
    if (match(TokenKind::For)) {
        return for_statement();
    }
    return expression_statement();
}

StmtPtr Parser::echo_statement() {
    auto value = assignment();
    return std::make_shared<EchoStmt>(value);
}

StmtPtr Parser::expression_statement() {
    auto expr = assignment();
    return std::make_shared<ExprStmt>(expr);
}

StmtPtr Parser::if_statement() {
    std::vector<IfBranch> branches;
    auto condition = assignment();
    auto body = block_until({TokenKind::ElseIf, TokenKind::Else, TokenKind::End});
    branches.push_back(IfBranch{condition, std::move(body)});

    while (match(TokenKind::ElseIf)) {
        auto elseif_condition = assignment();
        auto elseif_body = block_until({TokenKind::ElseIf, TokenKind::Else, TokenKind::End});
        branches.push_back(IfBranch{elseif_condition, std::move(elseif_body)});
    }

    std::vector<StmtPtr> else_body;
    if (match(TokenKind::Else)) {
        else_body = block_until({TokenKind::End});
    }

    consume(TokenKind::End, "expected 'end' to close if statement");
    return std::make_shared<IfStmt>(std::move(branches), std::move(else_body));
}

std::vector<StmtPtr> Parser::block_until(std::initializer_list<TokenKind> terminators) {
    std::vector<StmtPtr> stmts;
    while (!is_at_end() && std::find(terminators.begin(), terminators.end(), peek().kind) == terminators.end()) {
        stmts.push_back(declaration());
        match(TokenKind::Semicolon);
    }
    if (is_at_end()) {
        error(peek(), "unexpected end of file in block");
    }
    return stmts;
}

StmtPtr Parser::while_statement() {
    auto condition = assignment();
    auto body = block_until({TokenKind::End});
    consume(TokenKind::End, "expected 'end' after while loop");
    return std::make_shared<WhileStmt>(condition, std::move(body));
}

StmtPtr Parser::for_statement() {
    if (!match(TokenKind::Identifier)) {
        error(peek(), "expected identifier after 'for'");
    }
    std::string first = previous().lexeme;
    std::optional<std::string> index_name;
    std::string value_name;
    if (match(TokenKind::Comma)) {
        index_name = first;
        if (!match(TokenKind::Identifier)) {
            error(peek(), "expected second identifier in for loop");
        }
        value_name = previous().lexeme;
    } else {
        value_name = first;
    }
    if (!match(TokenKind::In)) {
        error(peek(), "expected 'in' in for loop");
    }
    auto iterable = assignment();
    auto body = block_until({TokenKind::End});
    consume(TokenKind::End, "expected 'end' after for loop");
    return std::make_shared<ForStmt>(std::move(index_name), value_name, iterable, std::move(body));
}

const Token& Parser::peek() const { return tokens_[current_]; }

const Token& Parser::previous() const { return tokens_[current_ - 1]; }

bool Parser::match(TokenKind kind) { return match({kind}); }

bool Parser::match(std::initializer_list<TokenKind> kinds) {
    for (auto kind : kinds) {
        if (check(kind)) {
            advance();
            return true;
        }
    }
    return false;
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) {
        return kind == TokenKind::EndOfFile;
    }
    return peek().kind == kind;
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::is_at_end() const { return peek().kind == TokenKind::EndOfFile; }

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    error(peek(), message);
}

[[noreturn]] void Parser::error(const Token& token, const std::string& message) {
    throw PolonioError(ErrorKind::Parse, message, path_, token.span.start);
}

} // namespace polonio
