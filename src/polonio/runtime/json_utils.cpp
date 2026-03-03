#include "polonio/runtime/json_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "polonio/runtime/output.h"

namespace polonio {

namespace {

class JsonParser {
public:
    JsonParser(const std::string& text, const JsonErrorFn& on_error)
        : text_(text), on_error_(on_error) {}

    Value parse() {
        skip_ws();
        Value value = parse_value();
        skip_ws();
        if (!is_end()) {
            error("invalid trailing data");
        }
        return value;
    }

private:
    void error(const std::string& message) const { on_error_(message); }

    bool is_end() const { return pos_ >= text_.size(); }

    char peek() const { return is_end() ? '\0' : text_[pos_]; }

    char advance() { return is_end() ? '\0' : text_[pos_++]; }

    void skip_ws() {
        while (!is_end()) {
            char ch = peek();
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                advance();
            } else {
                break;
            }
        }
    }

    Value parse_value() {
        if (is_end()) {
            error("unexpected end of input");
            return Value();
        }
        char ch = peek();
        if (ch == '"') {
            return Value(parse_string());
        }
        if (ch == '{') {
            return Value(parse_object());
        }
        if (ch == '[') {
            return Value(parse_array());
        }
        if (ch == 't' || ch == 'f' || ch == 'n') {
            return parse_literal();
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return Value(parse_number());
        }
        error("invalid value");
        return Value();
    }

    std::string parse_string() {
        if (advance() != '"') {
            error("expected string");
        }
        std::string out;
        while (!is_end()) {
            char ch = advance();
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                if (is_end()) {
                    error("unterminated escape");
                }
                char esc = advance();
                switch (esc) {
                case '"':
                case '\\':
                case '/': out.push_back(esc); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: error("invalid escape");
                }
            } else {
                if (static_cast<unsigned char>(ch) < 0x20) {
                    error("invalid control character");
                }
                out.push_back(ch);
            }
        }
        error("unterminated string");
        return std::string();
    }

    Value parse_literal() {
        if (match_literal("true")) {
            return Value(true);
        }
        if (match_literal("false")) {
            return Value(false);
        }
        if (match_literal("null")) {
            return Value();
        }
        error("invalid literal");
        return Value();
    }

    bool match_literal(const char* literal) {
        std::size_t len = std::strlen(literal);
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    Value::Object parse_object() {
        if (advance() != '{') {
            error("expected object");
        }
        Value::Object result;
        skip_ws();
        if (peek() == '}') {
            advance();
            return result;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                error("expected string key");
            }
            std::string key = parse_string();
            skip_ws();
            if (advance() != ':') {
                error("expected ':' in object");
            }
            skip_ws();
            Value value = parse_value();
            result[key] = value;
            skip_ws();
            char ch = advance();
            if (ch == '}') {
                break;
            }
            if (ch != ',') {
                error("expected ',' in object");
            }
            skip_ws();
        }
        return result;
    }

    Value::Array parse_array() {
        if (advance() != '[') {
            error("expected array");
        }
        Value::Array result;
        skip_ws();
        if (peek() == ']') {
            advance();
            return result;
        }
        while (true) {
            skip_ws();
            result.emplace_back(parse_value());
            skip_ws();
            char ch = advance();
            if (ch == ']') {
                break;
            }
            if (ch != ',') {
                error("expected ',' in array");
            }
            skip_ws();
        }
        return result;
    }

    double parse_number() {
        std::size_t start = pos_;
        if (peek() == '-') {
            advance();
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            error("invalid number");
        }
        if (peek() == '0') {
            advance();
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        if (peek() == '.') {
            advance();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                error("invalid number");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                error("invalid number");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        std::string slice = text_.substr(start, pos_ - start);
        try {
            return std::stod(slice);
        } catch (...) {
            error("invalid number");
        }
        return 0.0;
    }

    const std::string& text_;
    const JsonErrorFn& on_error_;
    std::size_t pos_ = 0;
};

void append_string(const std::string& input, std::string& out) {
    out.push_back('"');
    for (char ch : input) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch));
                out += oss.str();
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}

void serialize_impl(const Value& value, std::string& out, const JsonErrorFn& on_error);

void serialize_array(const Value::ArrayPtr& arr, std::string& out, const JsonErrorFn& on_error) {
    out.push_back('[');
    if (arr && !arr->empty()) {
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) out.push_back(',');
            serialize_impl((*arr)[i], out, on_error);
        }
    }
    out.push_back(']');
}

void serialize_object(const Value::ObjectPtr& obj, std::string& out, const JsonErrorFn& on_error) {
    out.push_back('{');
    if (obj && !obj->empty()) {
        std::vector<std::string> keys;
        keys.reserve(obj->size());
        for (const auto& entry : *obj) {
            keys.push_back(entry.first);
        }
        std::sort(keys.begin(), keys.end());
        bool first = true;
        for (const auto& key : keys) {
            if (!first) out.push_back(',');
            first = false;
            append_string(key, out);
            out.push_back(':');
            auto it = obj->find(key);
            serialize_impl(it->second, out, on_error);
        }
    }
    out.push_back('}');
}

void serialize_impl(const Value& value, std::string& out, const JsonErrorFn& on_error) {
    std::visit(
        [&](const auto& alt) {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += alt ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream oss;
                oss << alt;
                out += oss.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                append_string(alt, out);
            } else if constexpr (std::is_same_v<T, Value::ArrayPtr>) {
                serialize_array(alt, out, on_error);
            } else if constexpr (std::is_same_v<T, Value::ObjectPtr>) {
                serialize_object(alt, out, on_error);
            } else {
                on_error("session value not serializable");
            }
        },
        value.storage());
}

void ensure_serializable_impl(const Value& value, const JsonErrorFn& on_error) {
    std::visit(
        [&](const auto& alt) {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, bool> ||
                          std::is_same_v<T, double> || std::is_same_v<T, std::string>) {
                (void)alt;
            } else if constexpr (std::is_same_v<T, Value::ArrayPtr>) {
                if (alt) {
                    for (const auto& entry : *alt) {
                        ensure_serializable_impl(entry, on_error);
                    }
                }
            } else if constexpr (std::is_same_v<T, Value::ObjectPtr>) {
                if (alt) {
                    for (const auto& entry : *alt) {
                        ensure_serializable_impl(entry.second, on_error);
                    }
                }
            } else {
                on_error("session value not serializable");
            }
        },
        value.storage());
}

} // namespace

Value parse_json_string(const std::string& text, const JsonErrorFn& on_error) {
    JsonParser parser(text, on_error);
    return parser.parse();
}

std::string serialize_json_value(const Value& value, const JsonErrorFn& on_error) {
    std::string out;
    serialize_impl(value, out, on_error);
    return out;
}

void ensure_json_serializable(const Value& value, const JsonErrorFn& on_error) {
    ensure_serializable_impl(value, on_error);
}

} // namespace polonio
