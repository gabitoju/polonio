#include "polonio/lexer/lexer.h"

#include <cctype>
#include <unordered_map>

#include "polonio/common/error.h"

namespace polonio {

namespace {

TokenKind keyword_kind(const std::string& identifier) {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"var", TokenKind::Var},
        {"function", TokenKind::Function},
        {"echo", TokenKind::Echo},
        {"true", TokenKind::True},
        {"false", TokenKind::False},
        {"null", TokenKind::Null},
        {"and", TokenKind::And},
        {"or", TokenKind::Or},
        {"not", TokenKind::Not},
        {"end", TokenKind::End},
        {"if", TokenKind::If},
        {"elseif", TokenKind::ElseIf},
        {"else", TokenKind::Else},
        {"for", TokenKind::For},
        {"in", TokenKind::In},
        {"while", TokenKind::While},
        {"return", TokenKind::Return},
    };

    auto it = keywords.find(identifier);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenKind::Identifier;
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_identifier_part(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

Lexer::Lexer(std::string input, std::string path)
    : input_(std::move(input)), path_(std::move(path)) {}

std::vector<Token> Lexer::scan_all() {
    std::vector<Token> tokens;
    while (!is_at_end()) {
        skip_whitespace();
        if (is_at_end()) {
            break;
        }

        char c = peek();
        if (is_identifier_start(c)) {
            tokens.push_back(identifier());
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(number());
        } else if (c == '\'' || c == '\"') {
            tokens.push_back(string_literal());
        } else {
            tokens.push_back(symbol());
        }
    }

    tokens.push_back(make_token(TokenKind::EndOfFile, "", location_, location_));
    return tokens;
}

char Lexer::peek() const {
    if (is_at_end()) {
        return '\0';
    }
    return input_[current_];
}

char Lexer::peek_next() const {
    if (current_ + 1 >= input_.size()) {
        return '\0';
    }
    return input_[current_ + 1];
}

char Lexer::advance() {
    char c = input_[current_++];
    location_ = polonio::advance(location_, c);
    return c;
}

bool Lexer::match(char expected) {
    if (is_at_end() || input_[current_] != expected) {
        return false;
    }
    current_++;
    location_ = polonio::advance(location_, expected);
    return true;
}

bool Lexer::is_at_end() const {
    return current_ >= input_.size();
}

void Lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\r' || c == '\t') {
            advance();
            continue;
        }
        if (c == '\n') {
            advance();
            continue;
        }
        break;
    }
}

Token Lexer::identifier() {
    Location start = location_;
    std::size_t start_index = current_;
    advance();
    while (is_identifier_part(peek())) {
        advance();
    }
    std::string text = input_.substr(start_index, current_ - start_index);
    TokenKind kind = keyword_kind(text);
    return make_token(kind, text, start, location_);
}

Token Lexer::number() {
    Location start = location_;
    std::size_t start_index = current_;
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    std::string text = input_.substr(start_index, current_ - start_index);
    return make_token(TokenKind::Number, text, start, location_);
}

Token Lexer::string_literal() {
    Location start = location_;
    std::size_t start_index = current_;
    char quote = advance();
    bool terminated = false;
    while (!is_at_end()) {
        char c = peek();
        if (c == quote) {
            advance();
            terminated = true;
            break;
        }
        if (c == '\\') {
            advance();
            if (is_at_end()) {
                break;
            }
            advance();
            continue;
        }
        advance();
    }

    if (!terminated) {
        throw PolonioError(ErrorKind::Lex, "unterminated string", path_, start);
    }

    std::string text = input_.substr(start_index, current_ - start_index);
    return make_token(TokenKind::String, text, start, location_);
}

Token Lexer::make_token(TokenKind kind, const std::string& lexeme, const Location& start, const Location& end) {
    return Token{kind, lexeme, Span{start, end}};
}

Token Lexer::symbol() {
    Location start = location_;
    char c = advance();
    switch (c) {
        case '(': return make_token(TokenKind::LeftParen, "(", start, location_);
        case ')': return make_token(TokenKind::RightParen, ")", start, location_);
        case '[': return make_token(TokenKind::LeftBracket, "[", start, location_);
        case ']': return make_token(TokenKind::RightBracket, "]", start, location_);
        case '{': return make_token(TokenKind::LeftBrace, "{", start, location_);
        case '}': return make_token(TokenKind::RightBrace, "}", start, location_);
        case ',': return make_token(TokenKind::Comma, ",", start, location_);
        case ':': return make_token(TokenKind::Colon, ":", start, location_);
        case ';': return make_token(TokenKind::Semicolon, ";", start, location_);
        case '+':
            if (match('=')) return make_token(TokenKind::PlusEqual, "+=", start, location_);
            return make_token(TokenKind::Plus, "+", start, location_);
        case '-':
            if (match('=')) return make_token(TokenKind::MinusEqual, "-=", start, location_);
            return make_token(TokenKind::Minus, "-", start, location_);
        case '*':
            if (match('=')) return make_token(TokenKind::StarEqual, "*=", start, location_);
            return make_token(TokenKind::Star, "*", start, location_);
        case '/':
            if (match('=')) return make_token(TokenKind::SlashEqual, "/=", start, location_);
            return make_token(TokenKind::Slash, "/", start, location_);
        case '%':
            if (match('=')) return make_token(TokenKind::PercentEqual, "%=", start, location_);
            return make_token(TokenKind::Percent, "%", start, location_);
        case '=':
            if (match('=')) return make_token(TokenKind::EqualEqual, "==", start, location_);
            return make_token(TokenKind::Equal, "=", start, location_);
        case '!':
            if (match('=')) return make_token(TokenKind::NotEqual, "!=", start, location_);
            throw PolonioError(ErrorKind::Lex, "unexpected character: !", path_, start);
        case '<':
            if (match('=')) return make_token(TokenKind::LessEqual, "<=", start, location_);
            return make_token(TokenKind::Less, "<", start, location_);
        case '>':
            if (match('=')) return make_token(TokenKind::GreaterEqual, ">=", start, location_);
            return make_token(TokenKind::Greater, ">", start, location_);
        case '.':
            if (match('.')) {
                if (match('=')) {
                    return make_token(TokenKind::DotDotEqual, "..=", start, location_);
                }
                return make_token(TokenKind::DotDot, "..", start, location_);
            }
            throw PolonioError(ErrorKind::Lex, "unexpected character: .", path_, start);
        default:
            throw PolonioError(ErrorKind::Lex, std::string("unexpected character: ") + c, path_, start);
    }
}

} // namespace polonio
