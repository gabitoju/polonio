#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "polonio/common/location.h"

namespace polonio {

enum class TokenKind {
    Identifier,
    Var,
    Function,
    Echo,
    True,
    False,
    Null,
    And,
    Or,
    Not,
    End,
    If,
    ElseIf,
    Else,
    For,
    In,
    While,
    Return,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Colon,
    Semicolon,
    Equal,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Less,
    Greater,
    EqualEqual,
    NotEqual,
    LessEqual,
    GreaterEqual,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    DotDot,
    DotDotEqual,
    Number,
    String,
    EndOfFile,
};

struct Token {
    TokenKind kind;
    std::string lexeme;
    Span span;
};

class Lexer {
public:
    explicit Lexer(std::string input, std::string path = {});

    std::vector<Token> scan_all();

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool is_at_end() const;

    void skip_whitespace();

    Token identifier();
    Token number();
    Token string_literal();
    Token symbol();

    Token make_token(TokenKind kind, const std::string& lexeme, const Location& start, const Location& end);

    std::string input_;
    std::string path_;
    std::size_t current_ = 0;
    Location location_ = Location::start();
};

} // namespace polonio
