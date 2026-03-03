#include "polonio/runtime/builtins.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

#include "polonio/common/error.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/output.h"
#include "polonio/runtime/cgi.h"
#include "polonio/common/location.h"

namespace polonio {

namespace {

const Value& ensure_arg(const std::string& name,
                        std::size_t index,
                        const std::vector<Value>& args,
                        Interpreter& interp,
                        const Location& loc) {
    if (index >= args.size()) {
        throw PolonioError(ErrorKind::Runtime,
                           name + ": expected at least " + std::to_string(index + 1) + " argument(s)",
                           interp.path(),
                           loc);
    }
    return args[index];
}

void write_output_values(Interpreter& interp, const std::vector<Value>& args) {
    for (const auto& value : args) {
        interp.write_text(OutputBuffer::value_to_string(value));
    }
}

ResponseContext* require_cgi_context(const std::string& name, Interpreter& interp, const Location& loc) {
    auto* ctx = interp.response_context();
    if (!ctx) {
        throw PolonioError(ErrorKind::Runtime, name + ": CGI mode only", interp.path(), loc);
    }
    if (ctx->headers_sent) {
        throw PolonioError(ErrorKind::Runtime, name + ": headers already sent", interp.path(), loc);
    }
    return ctx;
}

std::string trim(std::string s) {
    std::size_t first = s.find_first_not_of(" \t");
    std::size_t last = s.find_last_not_of(" \t");
    if (first == std::string::npos) return std::string();
    return s.substr(first, last - first + 1);
}

std::mt19937_64& global_rng() {
    static std::mt19937_64 rng(0xC0FFEE);
    return rng;
}

CGIContext* current_cgi_context(Interpreter& interp) { return interp.cgi_context(); }

std::string normalize_header_lookup(const std::string& name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (char ch : name) {
        if (ch == '_') {
            normalized.push_back('-');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

class JsonParser {
public:
    JsonParser(const std::string& text, Interpreter& interp, const Location& loc)
        : text_(text), interp_(interp), loc_(loc) {}

    Value parse() {
        skip_ws();
        Value result = parse_value();
        skip_ws();
        if (!is_end()) {
            error();
        }
        return result;
    }

private:
    void error() const {
        throw PolonioError(ErrorKind::Runtime, "request_json: invalid json", interp_.path(), loc_);
    }

    bool is_end() const { return pos_ >= text_.size(); }

    char peek() const { return is_end() ? '\0' : text_[pos_]; }

    char advance() {
        if (is_end()) {
            return '\0';
        }
        return text_[pos_++];
    }

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
            error();
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
        error();
        return Value();
    }

    std::string parse_string() {
        if (advance() != '"') {
            error();
        }
        std::string out;
        while (!is_end()) {
            char ch = advance();
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                if (is_end()) {
                    error();
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
                default: error();
                }
            } else {
                if (static_cast<unsigned char>(ch) < 0x20) {
                    error();
                }
                out.push_back(ch);
            }
        }
        error();
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
        error();
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
            error();
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
                error();
            }
            std::string key = parse_string();
            skip_ws();
            if (advance() != ':') {
                error();
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
                error();
            }
            skip_ws();
        }
        return result;
    }

    Value::Array parse_array() {
        if (advance() != '[') {
            error();
        }
        Value::Array values;
        skip_ws();
        if (peek() == ']') {
            advance();
            return values;
        }
        while (true) {
            skip_ws();
            values.emplace_back(parse_value());
            skip_ws();
            char ch = advance();
            if (ch == ']') {
                break;
            }
            if (ch != ',') {
                error();
            }
            skip_ws();
        }
        return values;
    }

    double parse_number() {
        std::size_t start = pos_;
        if (peek() == '-') {
            advance();
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            error();
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
                error();
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
                error();
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        std::string number = text_.substr(start, pos_ - start);
        try {
            return std::stod(number);
        } catch (...) {
            error();
        }
        return 0.0;
    }

    const std::string& text_;
    Interpreter& interp_;
    const Location& loc_;
    std::size_t pos_ = 0;
};
Value builtin_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_tostring(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_to_string(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_to_number(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_nl2br(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_print(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_println(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_htmlspecialchars(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_html_escape(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_substr(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_len(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_lower(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_upper(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_trim(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_replace(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_split(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_contains(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_starts_with(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_ends_with(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_abs(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_floor(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_ceil(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_round(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_pow(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_sqrt(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_rand(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_randint(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_min(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_max(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_null(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_bool(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_number(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_string(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_array(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_object(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_is_function(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_now(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_status(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_header(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_http_content_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_redirect(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_urlencode(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_urldecode(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_htmlspecialchars(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_date_parts(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_date_format(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_date_add_days(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_date_parse(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_request_body(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_request_header(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_request_headers(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_cookies(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_request_json(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_count(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_push(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_pop(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_shift(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_unshift(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_concat(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_join(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_range(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_slice(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_keys(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_has_key(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_get(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_set(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_values(Interpreter& interp, const std::vector<Value>& args, const Location& loc);

Value builtin_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("type", 0, args, interp, loc);
    return Value(value.type_name());
}

Value builtin_tostring(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("tostring", 0, args, interp, loc);
    return Value(OutputBuffer::value_to_string(value));
}

Value builtin_to_string(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("to_string", 0, args, interp, loc);
    return Value(OutputBuffer::value_to_string(value));
}

Value builtin_to_number(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("to_number", 0, args, interp, loc);
    if (std::holds_alternative<double>(value.storage())) {
        return value;
    }
    if (std::holds_alternative<bool>(value.storage())) {
        return Value(std::get<bool>(value.storage()) ? 1.0 : 0.0);
    }
    if (std::holds_alternative<std::monostate>(value.storage())) {
        return Value(0.0);
    }
    if (std::holds_alternative<std::string>(value.storage())) {
        std::string text = trim(std::get<std::string>(value.storage()));
        if (text.empty()) {
            return Value(0.0);
        }
        std::size_t idx = 0;
        try {
            double number = std::stod(text, &idx);
            if (idx != text.size()) {
                throw std::invalid_argument("trailing");
            }
            return Value(number);
        } catch (const std::exception&) {
            throw PolonioError(ErrorKind::Runtime, "to_number: invalid numeric string", interp.path(), loc);
        }
    }
    throw PolonioError(ErrorKind::Runtime, "to_number: unsupported type", interp.path(), loc);
}

Value builtin_nl2br(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("nl2br", 0, args, interp, loc);
    std::string input;
    if (std::holds_alternative<std::string>(value.storage())) {
        input = std::get<std::string>(value.storage());
    } else {
        input = OutputBuffer::value_to_string(value);
    }
    std::string output;
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
            output += "<br>\n";
            i += 2;
        } else if (input[i] == '\n' || input[i] == '\r') {
            output += "<br>\n";
            i += 1;
        } else {
            output.push_back(input[i]);
            i += 1;
        }
    }
    return Value(output);
}

Value builtin_print(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    (void)loc;
    write_output_values(interp, args);
    return Value();
}

Value builtin_println(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    (void)loc;
    write_output_values(interp, args);
    interp.write_text("\n");
    return Value();
}

std::string describe_value_for_debug(const Value& value) {
    return std::visit(
        [](const auto& alt) -> std::string {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return alt ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                return OutputBuffer::value_to_string(Value(alt));
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + alt + "\"";
            } else if constexpr (std::is_same_v<T, Value::ArrayPtr>) {
                std::size_t len = alt ? alt->size() : 0;
                return "array(len=" + std::to_string(len) + ")";
            } else if constexpr (std::is_same_v<T, Value::ObjectPtr>) {
                std::size_t len = alt ? alt->size() : 0;
                return "object(len=" + std::to_string(len) + ")";
            } else if constexpr (std::is_same_v<T, BuiltinFunction>) {
                return "function(name=" + alt.name + ")";
            } else {
                std::string name = alt.name.empty() ? "<anon>" : alt.name;
                return "function(name=" + name + ")";
            }
        },
        value.storage());
}

Value builtin_debug(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "debug: expected 1 argument", interp.path(), loc);
    }
    Value value = args[0];
    std::cerr << describe_value_for_debug(value) << std::endl;
    return Value();
}

Value builtin_htmlspecialchars(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("htmlspecialchars", 0, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(value);
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return Value(out);
}

Value builtin_html_escape(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "html_escape: expected 1 argument", interp.path(), loc);
    }
    std::string text = OutputBuffer::value_to_string(args[0]);
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return Value(out);
}

int coerce_int(const std::string& name, const std::string& param, const Value& value, Interpreter& interp, const Location& loc) {
    if (!std::holds_alternative<double>(value.storage())) {
        throw PolonioError(ErrorKind::Runtime, name + ": expected number for " + param, interp.path(), loc);
    }
    double number = std::get<double>(value.storage());
    return static_cast<int>(number);
}

std::size_t normalize_start_index(int start, std::size_t length) {
    long long normalized = start;
    if (normalized < 0) {
        normalized = static_cast<long long>(length) + normalized;
    }
    if (normalized < 0) normalized = 0;
    if (normalized > static_cast<long long>(length)) normalized = static_cast<long long>(length);
    return static_cast<std::size_t>(normalized);
}

Value builtin_substr(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() < 2 || args.size() > 3) {
        throw PolonioError(ErrorKind::Runtime, "substr: expected 2 or 3 arguments", interp.path(), loc);
    }
    std::string text = OutputBuffer::value_to_string(args[0]);
    int start_raw = coerce_int("substr", "start", args[1], interp, loc);
    std::size_t len = text.size();
    std::size_t start = normalize_start_index(start_raw, len);
    if (start >= len) {
        return Value(std::string());
    }
    std::size_t end = len;
    if (args.size() == 3) {
        int length_raw = coerce_int("substr", "length", args[2], interp, loc);
        if (length_raw <= 0) {
            return Value(std::string());
        }
        end = start + static_cast<std::size_t>(length_raw);
        if (end > len) end = len;
    }
    if (end < start) end = start;
    return Value(text.substr(start, end - start));
}

Value builtin_len(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("len", 0, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(value);
    return Value(static_cast<double>(text.size()));
}

Value builtin_lower(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("lower", 0, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(value);
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return Value(text);
}

Value builtin_upper(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("upper", 0, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(value);
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return Value(text);
}

Value builtin_trim(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("trim", 0, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(value);
    auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    std::size_t start = 0;
    while (start < text.size() && is_ws(text[start])) {
        start++;
    }
    std::size_t end = text.size();
    while (end > start && is_ws(text[end - 1])) {
        end--;
    }
    return Value(text.substr(start, end - start));
}

Value builtin_replace(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value source = ensure_arg("replace", 0, args, interp, loc);
    Value from = ensure_arg("replace", 1, args, interp, loc);
    Value to = ensure_arg("replace", 2, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(source);
    std::string from_str = OutputBuffer::value_to_string(from);
    std::string to_str = OutputBuffer::value_to_string(to);
    if (from_str.empty()) {
        return Value(text);
    }
    std::string result;
    std::size_t pos = 0;
    while (true) {
        std::size_t found = text.find(from_str, pos);
        if (found == std::string::npos) {
            result.append(text.substr(pos));
            break;
        }
        result.append(text.substr(pos, found - pos));
        result.append(to_str);
        pos = found + from_str.size();
    }
    return Value(result);
}

Value builtin_split(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value source = ensure_arg("split", 0, args, interp, loc);
    Value sep_value = ensure_arg("split", 1, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(source);
    std::string sep = OutputBuffer::value_to_string(sep_value);
    Value::Array parts;
    if (sep.empty()) {
        parts.push_back(Value(text));
        return Value(parts);
    }
    std::size_t pos = 0;
    while (true) {
        std::size_t found = text.find(sep, pos);
        if (found == std::string::npos) {
            parts.push_back(Value(text.substr(pos)));
            break;
        }
        parts.push_back(Value(text.substr(pos, found - pos)));
        pos = found + sep.size();
    }
    return Value(parts);
}

Value builtin_contains(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value haystack = ensure_arg("contains", 0, args, interp, loc);
    Value needle = ensure_arg("contains", 1, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(haystack);
    std::string sub = OutputBuffer::value_to_string(needle);
    return Value(text.find(sub) != std::string::npos);
}

Value builtin_starts_with(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value haystack = ensure_arg("starts_with", 0, args, interp, loc);
    Value needle = ensure_arg("starts_with", 1, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(haystack);
    std::string prefix = OutputBuffer::value_to_string(needle);
    if (prefix.size() > text.size()) {
        return Value(false);
    }
    return Value(text.compare(0, prefix.size(), prefix) == 0);
}

Value builtin_ends_with(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value haystack = ensure_arg("ends_with", 0, args, interp, loc);
    Value needle = ensure_arg("ends_with", 1, args, interp, loc);
    std::string text = OutputBuffer::value_to_string(haystack);
    std::string suffix = OutputBuffer::value_to_string(needle);
    if (suffix.size() > text.size()) {
        return Value(false);
    }
    return Value(text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
}

Value builtin_abs(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value val = ensure_arg("abs", 0, args, interp, loc);
    if (!std::holds_alternative<double>(val.storage())) {
        throw PolonioError(ErrorKind::Runtime, "abs: expected number", interp.path(), loc);
    }
    return Value(std::fabs(std::get<double>(val.storage())));
}

Value builtin_floor(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value val = ensure_arg("floor", 0, args, interp, loc);
    if (!std::holds_alternative<double>(val.storage())) {
        throw PolonioError(ErrorKind::Runtime, "floor: expected number", interp.path(), loc);
    }
    return Value(std::floor(std::get<double>(val.storage())));
}

Value builtin_ceil(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value val = ensure_arg("ceil", 0, args, interp, loc);
    if (!std::holds_alternative<double>(val.storage())) {
        throw PolonioError(ErrorKind::Runtime, "ceil: expected number", interp.path(), loc);
    }
    return Value(std::ceil(std::get<double>(val.storage())));
}

Value builtin_round(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value val = ensure_arg("round", 0, args, interp, loc);
    if (!std::holds_alternative<double>(val.storage())) {
        throw PolonioError(ErrorKind::Runtime, "round: expected number", interp.path(), loc);
    }
    return Value(std::round(std::get<double>(val.storage())));
}

Value builtin_pow(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value base = ensure_arg("pow", 0, args, interp, loc);
    Value exponent = ensure_arg("pow", 1, args, interp, loc);
    if (!std::holds_alternative<double>(base.storage()) || !std::holds_alternative<double>(exponent.storage())) {
        throw PolonioError(ErrorKind::Runtime, "pow: expected numbers", interp.path(), loc);
    }
    double result = std::pow(std::get<double>(base.storage()), std::get<double>(exponent.storage()));
    return Value(result);
}

Value builtin_sqrt(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("sqrt", 0, args, interp, loc);
    if (!std::holds_alternative<double>(value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "sqrt: expected number", interp.path(), loc);
    }
    double number = std::get<double>(value.storage());
    if (number < 0) {
        throw PolonioError(ErrorKind::Runtime, "sqrt: negative input", interp.path(), loc);
    }
    return Value(std::sqrt(number));
}

Value builtin_rand(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    (void)loc;
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "rand: expected 0 arguments", interp.path(), loc);
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return Value(dist(global_rng()));
}

Value builtin_randint(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value min_value = ensure_arg("randint", 0, args, interp, loc);
    Value max_value = ensure_arg("randint", 1, args, interp, loc);
    if (!std::holds_alternative<double>(min_value.storage()) || !std::holds_alternative<double>(max_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "randint: expected numbers", interp.path(), loc);
    }
    int min_int = static_cast<int>(std::get<double>(min_value.storage()));
    int max_int = static_cast<int>(std::get<double>(max_value.storage()));
    if (max_int < min_int) {
        throw PolonioError(ErrorKind::Runtime, "randint: invalid range", interp.path(), loc);
    }
    std::uniform_int_distribution<int> dist(min_int, max_int);
    return Value(static_cast<double>(dist(global_rng())));
}

Value builtin_min(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value a = ensure_arg("min", 0, args, interp, loc);
    Value b = ensure_arg("min", 1, args, interp, loc);
    if (!std::holds_alternative<double>(a.storage()) || !std::holds_alternative<double>(b.storage())) {
        throw PolonioError(ErrorKind::Runtime, "min: expected numbers", interp.path(), loc);
    }
    return Value(std::min(std::get<double>(a.storage()), std::get<double>(b.storage())));
}

Value builtin_max(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value a = ensure_arg("max", 0, args, interp, loc);
    Value b = ensure_arg("max", 1, args, interp, loc);
    if (!std::holds_alternative<double>(a.storage()) || !std::holds_alternative<double>(b.storage())) {
        throw PolonioError(ErrorKind::Runtime, "max: expected numbers", interp.path(), loc);
    }
    return Value(std::max(std::get<double>(a.storage()), std::get<double>(b.storage())));
}

Value builtin_is_null(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_null", 0, args, interp, loc);
    return Value(std::holds_alternative<std::monostate>(value.storage()));
}

Value builtin_is_bool(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_bool", 0, args, interp, loc);
    return Value(std::holds_alternative<bool>(value.storage()));
}

Value builtin_is_number(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_number", 0, args, interp, loc);
    return Value(std::holds_alternative<double>(value.storage()));
}

Value builtin_is_string(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_string", 0, args, interp, loc);
    return Value(std::holds_alternative<std::string>(value.storage()));
}

Value builtin_is_array(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_array", 0, args, interp, loc);
    return Value(std::holds_alternative<Value::ArrayPtr>(value.storage()));
}

Value builtin_is_object(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_object", 0, args, interp, loc);
    return Value(std::holds_alternative<Value::ObjectPtr>(value.storage()));
}

Value builtin_is_function(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("is_function", 0, args, interp, loc);
    return Value(std::holds_alternative<FunctionValue>(value.storage()) || std::holds_alternative<BuiltinFunction>(value.storage()));
}

Value builtin_now([[maybe_unused]] Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "now: expected 0 arguments", interp.path(), loc);
    }
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return Value(static_cast<double>(seconds));
}

Value builtin_status(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    auto* ctx = require_cgi_context("status", interp, loc);
    Value code_value = ensure_arg("status", 0, args, interp, loc);
    if (!std::holds_alternative<double>(code_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "status: expected number", interp.path(), loc);
    }
    double code = std::get<double>(code_value.storage());
    double integral;
    if (std::modf(code, &integral) != 0.0) {
        throw PolonioError(ErrorKind::Runtime, "status: expected integer", interp.path(), loc);
    }
    int status_code = static_cast<int>(integral);
    if (status_code < 100 || status_code > 599) {
        throw PolonioError(ErrorKind::Runtime, "status: code out of range", interp.path(), loc);
    }
    ctx->set_status(status_code);
    return Value();
}

Value builtin_header(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() == 0) {
        throw PolonioError(ErrorKind::Runtime, "header: expected arguments", interp.path(), loc);
    }
    if (args.size() > 2) {
        throw PolonioError(ErrorKind::Runtime, "header: expected 1 or 2 arguments", interp.path(), loc);
    }
    auto* ctx = require_cgi_context("header", interp, loc);
    std::string name;
    std::string value;
    if (args.size() == 1) {
        std::string line = OutputBuffer::value_to_string(args[0]);
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            throw PolonioError(ErrorKind::Runtime, "header: invalid header line", interp.path(), loc);
        }
        name = trim(line.substr(0, colon));
        value = trim(line.substr(colon + 1));
        if (name.empty() || value.empty()) {
            throw PolonioError(ErrorKind::Runtime, "header: invalid header line", interp.path(), loc);
        }
    } else {
        name = trim(OutputBuffer::value_to_string(args[0]));
        value = trim(OutputBuffer::value_to_string(args[1]));
        if (name.empty()) {
            throw PolonioError(ErrorKind::Runtime, "header: expected header name", interp.path(), loc);
        }
        if (value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
            throw PolonioError(ErrorKind::Runtime, "header: invalid header value", interp.path(), loc);
        }
    }
    ctx->add_header(name, value);
    return Value();
}

Value builtin_http_content_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    auto* ctx = require_cgi_context("http_content_type", interp, loc);
    std::string value = trim(OutputBuffer::value_to_string(ensure_arg("http_content_type", 0, args, interp, loc)));
    if (value.empty()) {
        throw PolonioError(ErrorKind::Runtime, "http_content_type: expected value", interp.path(), loc);
    }
    ctx->add_header("Content-Type", value);
    return Value();
}

Value builtin_redirect(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    auto* ctx = require_cgi_context("redirect", interp, loc);
    std::string target = trim(OutputBuffer::value_to_string(ensure_arg("redirect", 0, args, interp, loc)));
    if (target.empty()) {
        throw PolonioError(ErrorKind::Runtime, "redirect: expected location", interp.path(), loc);
    }
    int status = 302;
    if (args.size() >= 2) {
        Value code_value = ensure_arg("redirect", 1, args, interp, loc);
        if (!std::holds_alternative<double>(code_value.storage())) {
            throw PolonioError(ErrorKind::Runtime, "redirect: expected status code", interp.path(), loc);
        }
        double code = std::get<double>(code_value.storage());
        double integral;
        if (std::modf(code, &integral) != 0.0) {
            throw PolonioError(ErrorKind::Runtime, "redirect: expected integer status code", interp.path(), loc);
        }
        status = static_cast<int>(integral);
        if (status < 300 || status > 399) {
            throw PolonioError(ErrorKind::Runtime, "redirect: status code must be 3xx", interp.path(), loc);
        }
    }
    ctx->set_status(status);
    ctx->add_header("Location", target);
    return Value();
}

Value builtin_urlencode(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    std::string text = OutputBuffer::value_to_string(ensure_arg("urlencode", 0, args, interp, loc));
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char ch : text) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0xF]);
            out.push_back(hex[ch & 0xF]);
        }
    }
    return Value(out);
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

Value builtin_urldecode(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    std::string text = OutputBuffer::value_to_string(ensure_arg("urldecode", 0, args, interp, loc));
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= text.size()) {
                throw PolonioError(ErrorKind::Runtime, "urldecode: incomplete escape", interp.path(), loc);
            }
            int hi = hex_value(text[i + 1]);
            int lo = hex_value(text[i + 2]);
            if (hi < 0 || lo < 0) {
                throw PolonioError(ErrorKind::Runtime, "urldecode: invalid escape", interp.path(), loc);
            }
            char decoded = static_cast<char>((hi << 4) | lo);
            out.push_back(decoded);
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return Value(out);
}

Value builtin_date_parts(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value epoch = ensure_arg("date_parts", 0, args, interp, loc);
    if (!std::holds_alternative<double>(epoch.storage())) {
        throw PolonioError(ErrorKind::Runtime, "date_parts: expected number", interp.path(), loc);
    }
    time_t seconds = static_cast<time_t>(std::floor(std::get<double>(epoch.storage())));
    std::tm tm = {};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    Value::Object result;
    result["year"] = Value(static_cast<double>(tm.tm_year + 1900));
    result["month"] = Value(static_cast<double>(tm.tm_mon + 1));
    result["day"] = Value(static_cast<double>(tm.tm_mday));
    result["hour"] = Value(static_cast<double>(tm.tm_hour));
    result["minute"] = Value(static_cast<double>(tm.tm_min));
    result["second"] = Value(static_cast<double>(tm.tm_sec));
    return Value(std::move(result));
}

std::string format_component(int value, int width) {
    std::string s = std::to_string(value);
    if (static_cast<int>(s.size()) < width) {
        s.insert(s.begin(), width - s.size(), '0');
    }
    return s;
}

Value builtin_date_format(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value epoch = ensure_arg("date_format", 0, args, interp, loc);
    Value fmt_value = ensure_arg("date_format", 1, args, interp, loc);
    if (!std::holds_alternative<double>(epoch.storage())) {
        throw PolonioError(ErrorKind::Runtime, "date_format: expected number", interp.path(), loc);
    }
    std::string fmt = OutputBuffer::value_to_string(fmt_value);
    time_t seconds = static_cast<time_t>(std::floor(std::get<double>(epoch.storage())));
    std::tm tm = {};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    std::string out = fmt;
    auto replace_token = [&](const std::string& token, const std::string& value) {
        std::size_t pos = 0;
        while ((pos = out.find(token, pos)) != std::string::npos) {
            out.replace(pos, token.size(), value);
            pos += value.size();
        }
    };
    replace_token("YYYY", format_component(tm.tm_year + 1900, 4));
    replace_token("MM", format_component(tm.tm_mon + 1, 2));
    replace_token("DD", format_component(tm.tm_mday, 2));
    replace_token("HH", format_component(tm.tm_hour, 2));
    replace_token("mm", format_component(tm.tm_min, 2));
    replace_token("SS", format_component(tm.tm_sec, 2));
    return Value(out);
}

Value builtin_date_add_days(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value epoch = ensure_arg("date_add_days", 0, args, interp, loc);
    Value days = ensure_arg("date_add_days", 1, args, interp, loc);
    if (!std::holds_alternative<double>(epoch.storage()) || !std::holds_alternative<double>(days.storage())) {
        throw PolonioError(ErrorKind::Runtime, "date_add_days: expected numbers", interp.path(), loc);
    }
    double seconds = std::get<double>(epoch.storage());
    double day_count = std::get<double>(days.storage());
    return Value(seconds + day_count * 86400.0);
}

bool parse_fixed_number(const std::string& text, std::size_t start, std::size_t length, int& out) {
    if (start + length > text.size()) return false;
    int value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        char ch = text[start + i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    out = value;
    return true;
}

bool parse_date_time_fields(const std::string& text,
                            int& year,
                            int& month,
                            int& day,
                            int& hour,
                            int& minute,
                            int& second,
                            bool include_time,
                            char separator) {
    if (!parse_fixed_number(text, 0, 4, year)) return false;
    if (text.size() < 7 || text[4] != '-' || text[7] != '-') return false;
    if (!parse_fixed_number(text, 5, 2, month)) return false;
    if (!parse_fixed_number(text, 8, 2, day)) return false;
    if (!include_time) {
        hour = minute = second = 0;
        return text.size() == 10;
    }
    if (text.size() < 19) return false;
    if (text[10] != separator) return false;
    if (!parse_fixed_number(text, 11, 2, hour)) return false;
    if (text[13] != ':') return false;
    if (!parse_fixed_number(text, 14, 2, minute)) return false;
    if (text[16] != ':') return false;
    if (!parse_fixed_number(text, 17, 2, second)) return false;
    return text.size() == 19;
}

bool is_leap_year(int year) {
    if (year % 400 == 0) return true;
    if (year % 100 == 0) return false;
    return year % 4 == 0;
}

bool validate_date_fields(int year, int month, int day, int hour, int minute, int second) {
    if (year < 0 || year > 9999) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = days_in_month[month - 1];
    if (month == 2 && is_leap_year(year)) dim = 29;
    if (day > dim) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;
    return true;
}

long long days_since_epoch(int year, int month, int day) {
    // Convert to days since 1970-01-01 using algorithm based on civil_from_days inverse.
    int y = year;
    int m = month;
    int d = day;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

Value builtin_date_parse(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    std::string input = OutputBuffer::value_to_string(ensure_arg("date_parse", 0, args, interp, loc));
    bool include_time = false;
    char separator = ' ';
    if (input.size() == 10) {
        include_time = false;
    } else if (input.size() == 19 && input[10] == ' ') {
        include_time = true;
        separator = ' ';
    } else if (input.size() == 19 && input[10] == 'T') {
        include_time = true;
        separator = 'T';
    } else {
        throw PolonioError(ErrorKind::Runtime, "date_parse: invalid format", interp.path(), loc);
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!parse_date_time_fields(input, year, month, day, hour, minute, second, include_time, separator) ||
        !validate_date_fields(year, month, day, hour, minute, second)) {
        throw PolonioError(ErrorKind::Runtime, "date_parse: invalid format", interp.path(), loc);
    }
    long long days = days_since_epoch(year, month, day);
    long long seconds = days * 86400LL + hour * 3600 + minute * 60 + second;
    return Value(static_cast<double>(seconds));
}

Value builtin_request_body(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "request_body: expected 0 arguments", interp.path(), loc);
    }
    auto* ctx = current_cgi_context(interp);
    if (!ctx) {
        return Value(std::string());
    }
    return Value(ctx->body);
}

Value builtin_request_header(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    std::string header = OutputBuffer::value_to_string(ensure_arg("request_header", 0, args, interp, loc));
    auto* ctx = current_cgi_context(interp);
    if (!ctx) {
        return Value();
    }
    std::string key = normalize_header_lookup(header);
    auto it = ctx->headers.find(key);
    if (it == ctx->headers.end()) {
        return Value();
    }
    return it->second;
}

Value builtin_request_headers(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "request_headers: expected 0 arguments", interp.path(), loc);
    }
    auto* ctx = current_cgi_context(interp);
    if (!ctx) {
        return Value(Value::Object());
    }
    return Value(ctx->headers);
}

Value builtin_cookies(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "cookies: expected 0 arguments", interp.path(), loc);
    }
    auto* ctx = current_cgi_context(interp);
    if (!ctx) {
        return Value(Value::Object());
    }
    return Value(ctx->cookie);
}

Value builtin_request_json(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "request_json: expected 0 arguments", interp.path(), loc);
    }
    auto* ctx = current_cgi_context(interp);
    std::string body = ctx ? ctx->body : std::string();
    if (body.empty()) {
        return Value();
    }
    JsonParser parser(body, interp, loc);
    return parser.parse();
}

Value builtin_count(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("count", 0, args, interp, loc);
    if (std::holds_alternative<Value::ArrayPtr>(value.storage())) {
        auto arr = std::get<Value::ArrayPtr>(value.storage());
        return Value(static_cast<double>(arr ? arr->size() : 0));
    }
    if (std::holds_alternative<Value::ObjectPtr>(value.storage())) {
        auto obj = std::get<Value::ObjectPtr>(value.storage());
        return Value(static_cast<double>(obj ? obj->size() : 0));
    }
    throw PolonioError(ErrorKind::Runtime, "count: expected array or object", interp.path(), loc);
}

Value builtin_push(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value array_value = ensure_arg("push", 0, args, interp, loc);
    Value element = ensure_arg("push", 1, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "push: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    if (!arr) {
        arr = std::make_shared<Value::Array>();
        array_value = Value(Value::Array(*arr));
    }
    arr->push_back(element);
    return Value(static_cast<double>(arr->size()));
}

Value builtin_pop(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value array_value = ensure_arg("pop", 0, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "pop: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    if (!arr || arr->empty()) {
        return Value();
    }
    Value result = arr->back();
    arr->pop_back();
    return result;
}

Value builtin_shift(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value array_value = ensure_arg("shift", 0, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "shift: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    if (!arr || arr->empty()) {
        return Value();
    }
    Value result = (*arr)[0];
    arr->erase(arr->begin());
    return result;
}

Value builtin_unshift(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value array_value = ensure_arg("unshift", 0, args, interp, loc);
    Value element = ensure_arg("unshift", 1, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "unshift: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    if (!arr) {
        arr = std::make_shared<Value::Array>();
        array_value = Value(Value::Array(*arr));
    }
    arr->insert(arr->begin(), element);
    return Value(static_cast<double>(arr->size()));
}

Value builtin_concat(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value first = ensure_arg("concat", 0, args, interp, loc);
    Value second = ensure_arg("concat", 1, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(first.storage()) || !std::holds_alternative<Value::ArrayPtr>(second.storage())) {
        throw PolonioError(ErrorKind::Runtime, "concat: expected arrays", interp.path(), loc);
    }
    auto a = std::get<Value::ArrayPtr>(first.storage());
    auto b = std::get<Value::ArrayPtr>(second.storage());
    Value::Array result;
    if (a) {
        for (const auto& item : *a) {
            result.emplace_back(item);
        }
    }
    if (b) {
        for (const auto& item : *b) {
            result.emplace_back(item);
        }
    }
    return Value(std::move(result));
}

Value builtin_join(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value array_value = ensure_arg("join", 0, args, interp, loc);
    Value sep_value = ensure_arg("join", 1, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "join: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    std::string sep = OutputBuffer::value_to_string(sep_value);
    std::string result;
    if (arr) {
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) {
                result += sep;
            }
            result += OutputBuffer::value_to_string((*arr)[i]);
        }
    }
    return Value(result);
}

Value builtin_slice(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() < 2 || args.size() > 3) {
        throw PolonioError(ErrorKind::Runtime, "slice: expected 2 or 3 arguments", interp.path(), loc);
    }
    Value array_value = ensure_arg("slice", 0, args, interp, loc);
    if (!std::holds_alternative<Value::ArrayPtr>(array_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "slice: expected array", interp.path(), loc);
    }
    auto arr = std::get<Value::ArrayPtr>(array_value.storage());
    std::size_t size = arr ? arr->size() : 0;
    int start_raw = coerce_int("slice", "start", args[1], interp, loc);
    std::size_t start = normalize_start_index(start_raw, size);
    if (start >= size) {
        return Value(Value::Array());
    }
    std::size_t end = size;
    if (args.size() == 3) {
        int length_raw = coerce_int("slice", "length", args[2], interp, loc);
        if (length_raw <= 0) {
            return Value(Value::Array());
        }
        end = start + static_cast<std::size_t>(length_raw);
        if (end > size) end = size;
    }
    Value::Array result;
    if (arr) {
        for (std::size_t i = start; i < end; ++i) {
            result.emplace_back((*arr)[i]);
        }
    }
    return Value(std::move(result));
}

Value builtin_range(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value count = ensure_arg("range", 0, args, interp, loc);
    if (!std::holds_alternative<double>(count.storage())) {
        throw PolonioError(ErrorKind::Runtime, "range: expected number", interp.path(), loc);
    }
    double number = std::get<double>(count.storage());
    Value::Array values;
    if (number > 0) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(number); ++i) {
            values.emplace_back(static_cast<double>(i));
        }
    }
    return Value(std::move(values));
}

Value builtin_keys(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value object_value = ensure_arg("keys", 0, args, interp, loc);
    if (!std::holds_alternative<Value::ObjectPtr>(object_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "keys: expected object", interp.path(), loc);
    }
    auto obj = std::get<Value::ObjectPtr>(object_value.storage());
    std::vector<std::string> keys;
    if (obj) {
        keys.reserve(obj->size());
        for (const auto& entry : *obj) {
            keys.push_back(entry.first);
        }
    }
    std::sort(keys.begin(), keys.end());
    Value::Array values;
    for (const auto& key : keys) {
        values.emplace_back(key);
    }
    return Value(std::move(values));
}

Value builtin_has_key(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value object_value = ensure_arg("has_key", 0, args, interp, loc);
    Value key_value = ensure_arg("has_key", 1, args, interp, loc);
    if (!std::holds_alternative<Value::ObjectPtr>(object_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "has_key: expected object", interp.path(), loc);
    }
    auto obj = std::get<Value::ObjectPtr>(object_value.storage());
    std::string key = OutputBuffer::value_to_string(key_value);
    if (!obj) {
        return Value(false);
    }
    return Value(obj->find(key) != obj->end());
}

Value builtin_get(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value object_value = ensure_arg("get", 0, args, interp, loc);
    Value key_value = ensure_arg("get", 1, args, interp, loc);
    Value default_value = args.size() > 2 ? args[2] : Value();
    if (!std::holds_alternative<Value::ObjectPtr>(object_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "get: expected object", interp.path(), loc);
    }
    auto obj = std::get<Value::ObjectPtr>(object_value.storage());
    std::string key = OutputBuffer::value_to_string(key_value);
    if (!obj) {
        return default_value;
    }
    auto it = obj->find(key);
    if (it == obj->end()) {
        return default_value;
    }
    return it->second;
}

Value builtin_set(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value object_value = ensure_arg("set", 0, args, interp, loc);
    Value key_value = ensure_arg("set", 1, args, interp, loc);
    Value val = ensure_arg("set", 2, args, interp, loc);
    if (!std::holds_alternative<Value::ObjectPtr>(object_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "set: expected object", interp.path(), loc);
    }
    auto obj = std::get<Value::ObjectPtr>(object_value.storage());
    std::string key = OutputBuffer::value_to_string(key_value);
    if (!obj) {
        obj = std::make_shared<Value::Object>();
        object_value = Value(Value::Object(*obj));
    }
    (*obj)[key] = val;
    return val;
}

Value builtin_values(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value object_value = ensure_arg("values", 0, args, interp, loc);
    if (!std::holds_alternative<Value::ObjectPtr>(object_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "values: expected object", interp.path(), loc);
    }
    auto obj = std::get<Value::ObjectPtr>(object_value.storage());
    std::vector<std::string> keys;
    if (obj) {
        keys.reserve(obj->size());
        for (const auto& entry : *obj) {
            keys.push_back(entry.first);
        }
    }
    std::sort(keys.begin(), keys.end());
    Value::Array result;
    if (obj) {
        for (const auto& key : keys) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                result.emplace_back(it->second);
            }
        }
    }
    return Value(std::move(result));
}

} // namespace

void install_builtins(Env& env) {
    env.set_local("type", Value(BuiltinFunction{"type", builtin_type}));
    env.set_local("tostring", Value(BuiltinFunction{"tostring", builtin_tostring}));
    env.set_local("to_string", Value(BuiltinFunction{"to_string", builtin_to_string}));
    env.set_local("to_number", Value(BuiltinFunction{"to_number", builtin_to_number}));
    env.set_local("print", Value(BuiltinFunction{"print", builtin_print}));
    env.set_local("println", Value(BuiltinFunction{"println", builtin_println}));
    env.set_local("debug", Value(BuiltinFunction{"debug", builtin_debug}));
    env.set_local("nl2br", Value(BuiltinFunction{"nl2br", builtin_nl2br}));
    env.set_local("htmlspecialchars", Value(BuiltinFunction{"htmlspecialchars", builtin_htmlspecialchars}));
    env.set_local("html_escape", Value(BuiltinFunction{"html_escape", builtin_html_escape}));
    env.set_local("len", Value(BuiltinFunction{"len", builtin_len}));
    env.set_local("substr", Value(BuiltinFunction{"substr", builtin_substr}));
    env.set_local("lower", Value(BuiltinFunction{"lower", builtin_lower}));
    env.set_local("upper", Value(BuiltinFunction{"upper", builtin_upper}));
    env.set_local("trim", Value(BuiltinFunction{"trim", builtin_trim}));
    env.set_local("replace", Value(BuiltinFunction{"replace", builtin_replace}));
    env.set_local("split", Value(BuiltinFunction{"split", builtin_split}));
    env.set_local("contains", Value(BuiltinFunction{"contains", builtin_contains}));
    env.set_local("starts_with", Value(BuiltinFunction{"starts_with", builtin_starts_with}));
    env.set_local("ends_with", Value(BuiltinFunction{"ends_with", builtin_ends_with}));
    env.set_local("count", Value(BuiltinFunction{"count", builtin_count}));
    env.set_local("push", Value(BuiltinFunction{"push", builtin_push}));
    env.set_local("pop", Value(BuiltinFunction{"pop", builtin_pop}));
    env.set_local("shift", Value(BuiltinFunction{"shift", builtin_shift}));
    env.set_local("unshift", Value(BuiltinFunction{"unshift", builtin_unshift}));
    env.set_local("concat", Value(BuiltinFunction{"concat", builtin_concat}));
    env.set_local("join", Value(BuiltinFunction{"join", builtin_join}));
    env.set_local("slice", Value(BuiltinFunction{"slice", builtin_slice}));
    env.set_local("range", Value(BuiltinFunction{"range", builtin_range}));
    env.set_local("keys", Value(BuiltinFunction{"keys", builtin_keys}));
    env.set_local("has_key", Value(BuiltinFunction{"has_key", builtin_has_key}));
    env.set_local("get", Value(BuiltinFunction{"get", builtin_get}));
    env.set_local("set", Value(BuiltinFunction{"set", builtin_set}));
    env.set_local("values", Value(BuiltinFunction{"values", builtin_values}));
    env.set_local("abs", Value(BuiltinFunction{"abs", builtin_abs}));
    env.set_local("floor", Value(BuiltinFunction{"floor", builtin_floor}));
    env.set_local("ceil", Value(BuiltinFunction{"ceil", builtin_ceil}));
    env.set_local("round", Value(BuiltinFunction{"round", builtin_round}));
    env.set_local("pow", Value(BuiltinFunction{"pow", builtin_pow}));
    env.set_local("sqrt", Value(BuiltinFunction{"sqrt", builtin_sqrt}));
    env.set_local("rand", Value(BuiltinFunction{"rand", builtin_rand}));
    env.set_local("randint", Value(BuiltinFunction{"randint", builtin_randint}));
    env.set_local("min", Value(BuiltinFunction{"min", builtin_min}));
    env.set_local("max", Value(BuiltinFunction{"max", builtin_max}));
    env.set_local("is_null", Value(BuiltinFunction{"is_null", builtin_is_null}));
    env.set_local("is_bool", Value(BuiltinFunction{"is_bool", builtin_is_bool}));
    env.set_local("is_number", Value(BuiltinFunction{"is_number", builtin_is_number}));
    env.set_local("is_string", Value(BuiltinFunction{"is_string", builtin_is_string}));
    env.set_local("is_array", Value(BuiltinFunction{"is_array", builtin_is_array}));
    env.set_local("is_object", Value(BuiltinFunction{"is_object", builtin_is_object}));
    env.set_local("is_function", Value(BuiltinFunction{"is_function", builtin_is_function}));
    env.set_local("now", Value(BuiltinFunction{"now", builtin_now}));
    env.set_local("status", Value(BuiltinFunction{"status", builtin_status}));
    env.set_local("header", Value(BuiltinFunction{"header", builtin_header}));
    env.set_local("http_status", Value(BuiltinFunction{"http_status", builtin_status}));
    env.set_local("http_header", Value(BuiltinFunction{"http_header", builtin_header}));
    env.set_local("http_content_type", Value(BuiltinFunction{"http_content_type", builtin_http_content_type}));
    env.set_local("redirect", Value(BuiltinFunction{"redirect", builtin_redirect}));
    env.set_local("urlencode", Value(BuiltinFunction{"urlencode", builtin_urlencode}));
    env.set_local("urldecode", Value(BuiltinFunction{"urldecode", builtin_urldecode}));
    env.set_local("date_parts", Value(BuiltinFunction{"date_parts", builtin_date_parts}));
    env.set_local("date_format", Value(BuiltinFunction{"date_format", builtin_date_format}));
    env.set_local("date_add_days", Value(BuiltinFunction{"date_add_days", builtin_date_add_days}));
    env.set_local("date_parse", Value(BuiltinFunction{"date_parse", builtin_date_parse}));
    env.set_local("request_body", Value(BuiltinFunction{"request_body", builtin_request_body}));
    env.set_local("request_header", Value(BuiltinFunction{"request_header", builtin_request_header}));
    env.set_local("request_headers", Value(BuiltinFunction{"request_headers", builtin_request_headers}));
    env.set_local("cookies", Value(BuiltinFunction{"cookies", builtin_cookies}));
    env.set_local("request_json", Value(BuiltinFunction{"request_json", builtin_request_json}));
}

} // namespace polonio
