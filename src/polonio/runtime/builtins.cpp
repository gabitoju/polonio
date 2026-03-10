#include "polonio/runtime/builtins.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <limits>
#include <memory>
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
#include "polonio/runtime/json_utils.h"
#include "polonio/runtime/session.h"
#include "polonio/runtime/storage_ops.h"
#include "polonio/runtime/db.h"
#include "polonio/runtime/crypto.h"
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

std::string require_storage_path_arg(const std::string& builtin_name,
                                     const Value& value,
                                     Interpreter& interp,
                                     const Location& loc) {
    if (!std::holds_alternative<std::string>(value.storage())) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": path must be string",
                           interp.path(),
                           loc);
    }
    return std::get<std::string>(value.storage());
}

std::string require_string_value(const std::string& builtin_name,
                                 const Value& value,
                                 Interpreter& interp,
                                 const Location& loc,
                                 const std::string& message) {
    if (!std::holds_alternative<std::string>(value.storage())) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": " + message,
                           interp.path(),
                           loc);
    }
    return std::get<std::string>(value.storage());
}

Value::ArrayPtr require_array_value(const std::string& builtin_name,
                                    const Value& value,
                                    Interpreter& interp,
                                    const Location& loc,
                                    const std::string& message) {
    if (!std::holds_alternative<Value::ArrayPtr>(value.storage())) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": " + message,
                           interp.path(),
                           loc);
    }
    return std::get<Value::ArrayPtr>(value.storage());
}

struct SQLiteStatementDeleter {
    void operator()(sqlite3_stmt* stmt) const {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
};

using SQLiteStatementPtr = std::unique_ptr<sqlite3_stmt, SQLiteStatementDeleter>;

bool is_integral_double(double value) { return std::floor(value) == value; }

bool fits_int64(double value) {
    return value >= static_cast<double>(std::numeric_limits<sqlite3_int64>::min()) &&
           value <= static_cast<double>(std::numeric_limits<sqlite3_int64>::max());
}

void bind_sqlite_value(sqlite3_stmt* stmt,
                       int index,
                       const Value& value,
                       const std::string& builtin_name,
                       Interpreter& interp,
                       const Location& loc) {
    const auto& storage = value.storage();
    if (std::holds_alternative<std::monostate>(storage)) {
        sqlite3_bind_null(stmt, index);
        return;
    }
    if (std::holds_alternative<bool>(storage)) {
        sqlite3_bind_int64(stmt, index, std::get<bool>(storage) ? 1 : 0);
        return;
    }
    if (std::holds_alternative<double>(storage)) {
        double number = std::get<double>(storage);
        if (is_integral_double(number) && fits_int64(number)) {
            sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(number));
        } else {
            sqlite3_bind_double(stmt, index, number);
        }
        return;
    }
    if (std::holds_alternative<std::string>(storage)) {
        const std::string& text = std::get<std::string>(storage);
        sqlite3_bind_text(stmt,
                          index,
                          text.c_str(),
                          static_cast<int>(text.size()),
                          SQLITE_TRANSIENT);
        return;
    }
    throw PolonioError(ErrorKind::Runtime,
                       builtin_name + ": unsupported parameter type",
                       interp.path(),
                       loc);
}

void bind_sqlite_parameters(sqlite3_stmt* stmt,
                            const Value::ArrayPtr& params,
                            const std::string& builtin_name,
                            Interpreter& interp,
                            const Location& loc) {
    int expected = sqlite3_bind_parameter_count(stmt);
    int provided = 0;
    const Value::Array* array = params ? params.get() : nullptr;
    if (array) {
        provided = static_cast<int>(array->size());
    }
    if (provided != expected) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": expected " + std::to_string(expected) + " parameter(s), got " +
                               std::to_string(provided),
                           interp.path(),
                           loc);
    }
    if (!array) {
        return;
    }
    for (int i = 0; i < provided; ++i) {
        bind_sqlite_value(stmt, i + 1, (*array)[static_cast<std::size_t>(i)], builtin_name, interp, loc);
    }
}

Value sqlite_value_from_column(sqlite3_stmt* stmt, int index) {
    int column_type = sqlite3_column_type(stmt, index);
    switch (column_type) {
        case SQLITE_INTEGER:
            return Value(static_cast<double>(sqlite3_column_int64(stmt, index)));
        case SQLITE_FLOAT:
            return Value(sqlite3_column_double(stmt, index));
        case SQLITE_TEXT: {
            const unsigned char* text = sqlite3_column_text(stmt, index);
            int len = sqlite3_column_bytes(stmt, index);
            if (!text) {
                return Value(std::string());
            }
            return Value(std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(len)));
        }
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, index);
            int len = sqlite3_column_bytes(stmt, index);
            if (!blob) {
                return Value(std::string());
            }
            return Value(std::string(reinterpret_cast<const char*>(blob), static_cast<std::size_t>(len)));
        }
        case SQLITE_NULL:
        default:
            return Value();
    }
}

Value make_row_value(sqlite3_stmt* stmt) {
    Value::Object object;
    int column_count = sqlite3_column_count(stmt);
    for (int i = 0; i < column_count; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        std::string key = name ? std::string(name) : ("column" + std::to_string(i));
        object[key] = sqlite_value_from_column(stmt, i);
    }
    return Value(std::move(object));
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

constexpr int kPasswordHashIterations = 200000;
constexpr int kPasswordHashSaltLen = 16;
constexpr int kPasswordHashKeyLen = 32;
constexpr int kPasswordHashMaxIterations = 10000000;

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

SessionContext* require_session_context(const std::string& name,
                                        Interpreter& interp,
                                        const Location& loc) {
    auto* ctx = interp.session_context();
    if (!ctx) {
        throw PolonioError(ErrorKind::Runtime, name + ": sessions unavailable", interp.path(), loc);
    }
    if (ctx->is_cgi && ctx->secret_missing) {
        throw PolonioError(ErrorKind::Runtime, "missing session secret", interp.path(), loc);
    }
    return ctx;
}

std::string require_session_key(const std::string& builtin,
                                const Value& key_value,
                                Interpreter& interp,
                                const Location& loc) {
    if (!std::holds_alternative<std::string>(key_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, builtin + ": key must be string", interp.path(), loc);
    }
    return std::get<std::string>(key_value.storage());
}

void ensure_session_serializable(const Value& value,
                                 Interpreter& interp,
                                 const Location& loc) {
    ensure_json_serializable(value, [&](const std::string& message) {
        throw PolonioError(ErrorKind::Runtime, message, interp.path(), loc);
    });
}

std::string generate_random_token(Interpreter& interp, const Location& loc, int nbytes) {
    std::string bytes;
    if (!secure_random_bytes(bytes, static_cast<std::size_t>(nbytes))) {
        throw PolonioError(ErrorKind::Runtime, "random_token: secure RNG failure", interp.path(), loc);
    }
    return base64url_encode(bytes);
}

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
Value builtin_file_read(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_write(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_append(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_exists(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_delete(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_size(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_file_modified(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_dir_create(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_dir_list(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_dir_exists(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_db_connect(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_db_close(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_db_query(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_db_exec(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_db_last_insert_id(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
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
Value builtin_session_get(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_session_set(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_session_unset(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_session_clear(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_random_token(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_csrf_token(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_csrf_verify(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_hash_password(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_verify_password(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
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
    return parse_json_string(body, [&](const std::string&) {
        throw PolonioError(ErrorKind::Runtime, "request_json: invalid json", interp.path(), loc);
    });
}

Value builtin_session_get(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value key_value = ensure_arg("session_get", 0, args, interp, loc);
    auto* ctx = require_session_context("session_get", interp, loc);
    std::string key = require_session_key("session_get", key_value, interp, loc);
    auto it = ctx->data.find(key);
    if (it == ctx->data.end()) {
        return Value();
    }
    return it->second;
}

Value builtin_session_set(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value key_value = ensure_arg("session_set", 0, args, interp, loc);
    Value value = ensure_arg("session_set", 1, args, interp, loc);
    auto* ctx = require_session_context("session_set", interp, loc);
    std::string key = require_session_key("session_set", key_value, interp, loc);
    ensure_session_serializable(value, interp, loc);
    ctx->data[key] = value;
    ctx->dirty = true;
    return Value();
}

Value builtin_session_unset(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value key_value = ensure_arg("session_unset", 0, args, interp, loc);
    auto* ctx = require_session_context("session_unset", interp, loc);
    std::string key = require_session_key("session_unset", key_value, interp, loc);
    auto it = ctx->data.find(key);
    if (it != ctx->data.end()) {
        ctx->data.erase(it);
        ctx->dirty = true;
    }
    return Value();
}

Value builtin_session_clear(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "session_clear: expected 0 arguments", interp.path(), loc);
    }
    auto* ctx = require_session_context("session_clear", interp, loc);
    if (!ctx->data.empty()) {
        ctx->data.clear();
        ctx->dirty = true;
    }
    return Value();
}

Value builtin_random_token(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    int nbytes = coerce_int("random_token", "nbytes", ensure_arg("random_token", 0, args, interp, loc), interp, loc);
    if (nbytes < 1 || nbytes > 1024) {
        throw PolonioError(ErrorKind::Runtime, "random_token: nbytes must be between 1 and 1024", interp.path(), loc);
    }
    std::string token = generate_random_token(interp, loc, nbytes);
    return Value(token);
}

Value builtin_csrf_token(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (!args.empty()) {
        throw PolonioError(ErrorKind::Runtime, "csrf_token: expected 0 arguments", interp.path(), loc);
    }
    auto* session = require_session_context("csrf_token", interp, loc);
    auto it = session->data.find("_csrf");
    if (it != session->data.end() && std::holds_alternative<std::string>(it->second.storage())) {
        const std::string& existing = std::get<std::string>(it->second.storage());
        if (!existing.empty()) {
            return Value(existing);
        }
    }
    std::string token = generate_random_token(interp, loc, 32);
    session->data["_csrf"] = Value(token);
    session->dirty = true;
    return Value(token);
}

Value builtin_csrf_verify(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value token_value = ensure_arg("csrf_verify", 0, args, interp, loc);
    std::string provided;
    if (std::holds_alternative<std::string>(token_value.storage())) {
        provided = std::get<std::string>(token_value.storage());
    } else {
        return Value(false);
    }
    if (provided.empty()) {
        return Value(false);
    }
    auto* session = interp.session_context();
    if (!session) {
        return Value(false);
    }
    if (session->is_cgi && session->secret_missing) {
        return Value(false);
    }
    auto it = session->data.find("_csrf");
    if (it == session->data.end() || !std::holds_alternative<std::string>(it->second.storage())) {
        return Value(false);
    }
    const std::string& expected = std::get<std::string>(it->second.storage());
    if (expected.empty()) {
        return Value(false);
    }
    return Value(provided == expected);
}

Value builtin_hash_password(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value password_value = ensure_arg("hash_password", 0, args, interp, loc);
    if (!std::holds_alternative<std::string>(password_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "hash_password: expected string", interp.path(), loc);
    }
    const std::string& password = std::get<std::string>(password_value.storage());
    std::string salt;
    if (!secure_random_bytes(salt, kPasswordHashSaltLen)) {
        throw PolonioError(ErrorKind::Runtime, "hash_password: secure RNG failure", interp.path(), loc);
    }
    std::string dk = pbkdf2_hmac_sha256(password, salt, kPasswordHashIterations, kPasswordHashKeyLen);
    std::string salt_b64 = base64url_encode(salt);
    std::string dk_b64 = base64url_encode(dk);
    std::string hash = "pbkdf2_sha256$" + std::to_string(kPasswordHashIterations) + "$" + salt_b64 + "$" + dk_b64;
    return Value(hash);
}

Value builtin_verify_password(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value password_value = ensure_arg("verify_password", 0, args, interp, loc);
    Value hash_value = ensure_arg("verify_password", 1, args, interp, loc);
    if (!std::holds_alternative<std::string>(password_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "verify_password: password must be string", interp.path(), loc);
    }
    if (!std::holds_alternative<std::string>(hash_value.storage())) {
        throw PolonioError(ErrorKind::Runtime, "verify_password: hash must be string", interp.path(), loc);
    }
    const std::string& password = std::get<std::string>(password_value.storage());
    const std::string& hash = std::get<std::string>(hash_value.storage());

    std::vector<std::string> parts;
    parts.reserve(4);
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        std::size_t pos = hash.find('$', start);
        if (pos == std::string::npos) {
            break;
        }
        parts.emplace_back(hash.substr(start, pos - start));
        start = pos + 1;
    }
    parts.emplace_back(hash.substr(start));
    if (parts.size() != 4) {
        return Value(false);
    }
    if (parts[0] != "pbkdf2_sha256") {
        return Value(false);
    }
    long long iterations = 0;
    try {
        iterations = std::stoll(parts[1]);
    } catch (...) {
        return Value(false);
    }
    if (iterations <= 0 || iterations > kPasswordHashMaxIterations) {
        return Value(false);
    }
    std::string salt_bytes;
    std::string dk_bytes;
    if (!base64url_decode(parts[2], salt_bytes) || !base64url_decode(parts[3], dk_bytes)) {
        return Value(false);
    }
    if (salt_bytes.size() != static_cast<std::size_t>(kPasswordHashSaltLen) ||
        dk_bytes.size() != static_cast<std::size_t>(kPasswordHashKeyLen)) {
        return Value(false);
    }
    std::string computed = pbkdf2_hmac_sha256(password, salt_bytes, static_cast<int>(iterations), dk_bytes.size());
    if (computed.size() != dk_bytes.size()) {
        return Value(false);
    }
    return Value(constant_time_equals(computed, dk_bytes));
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

Value builtin_file_read(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "file_read: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_read", 0, args, interp, loc);
    std::string path = require_storage_path_arg("file_read", path_value, interp, loc);
    auto content = storage_file_read(path, interp, "file_read", loc);
    return Value(content);
}

Value builtin_file_write(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 2) {
        throw PolonioError(ErrorKind::Runtime, "file_write: expected 2 arguments", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_write", 0, args, interp, loc);
    const Value& content_value = ensure_arg("file_write", 1, args, interp, loc);
    std::string path = require_storage_path_arg("file_write", path_value, interp, loc);
    std::string content = OutputBuffer::value_to_string(content_value);
    storage_file_write(path, content, interp, "file_write", loc);
    return Value();
}

Value builtin_file_append(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 2) {
        throw PolonioError(ErrorKind::Runtime, "file_append: expected 2 arguments", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_append", 0, args, interp, loc);
    const Value& content_value = ensure_arg("file_append", 1, args, interp, loc);
    std::string path = require_storage_path_arg("file_append", path_value, interp, loc);
    std::string content = OutputBuffer::value_to_string(content_value);
    storage_file_append(path, content, interp, "file_append", loc);
    return Value();
}

Value builtin_file_exists(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "file_exists: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_exists", 0, args, interp, loc);
    std::string path = require_storage_path_arg("file_exists", path_value, interp, loc);
    bool exists = storage_file_exists(path, interp, "file_exists", loc);
    return Value(exists);
}

Value builtin_file_delete(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "file_delete: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_delete", 0, args, interp, loc);
    std::string path = require_storage_path_arg("file_delete", path_value, interp, loc);
    bool deleted = storage_file_delete(path, interp, "file_delete", loc);
    return Value(deleted);
}

Value builtin_file_size(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "file_size: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_size", 0, args, interp, loc);
    std::string path = require_storage_path_arg("file_size", path_value, interp, loc);
    auto size = storage_file_size(path, interp, "file_size", loc);
    return Value(static_cast<double>(size));
}

Value builtin_file_modified(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "file_modified: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("file_modified", 0, args, interp, loc);
    std::string path = require_storage_path_arg("file_modified", path_value, interp, loc);
    auto seconds = storage_file_modified(path, interp, "file_modified", loc);
    return Value(static_cast<double>(seconds));
}

Value builtin_dir_create(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "dir_create: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("dir_create", 0, args, interp, loc);
    std::string path = require_storage_path_arg("dir_create", path_value, interp, loc);
    bool created = storage_dir_create(path, interp, "dir_create", loc);
    return Value(created);
}

Value builtin_dir_list(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "dir_list: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("dir_list", 0, args, interp, loc);
    std::string path = require_storage_path_arg("dir_list", path_value, interp, loc);
    auto entries = storage_dir_list(path, interp, "dir_list", loc);
    Value::Array result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.emplace_back(entry);
    }
    return Value(std::move(result));
}

Value builtin_dir_exists(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "dir_exists: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("dir_exists", 0, args, interp, loc);
    std::string path = require_storage_path_arg("dir_exists", path_value, interp, loc);
    bool exists = storage_dir_exists(path, interp, "dir_exists", loc);
    return Value(exists);
}

Value builtin_db_connect(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 1) {
        throw PolonioError(ErrorKind::Runtime, "db_connect: expected 1 argument", interp.path(), loc);
    }
    const Value& path_value = ensure_arg("db_connect", 0, args, interp, loc);
    std::string relative = require_storage_path_arg("db_connect", path_value, interp, loc);
    auto* conn = interp.db_connection();
    if (!conn) {
        throw PolonioError(ErrorKind::Runtime, "db_connect: database unavailable", interp.path(), loc);
    }
    conn->connect_relative(relative, interp, "db_connect", loc);
    return Value();
}

Value builtin_db_close(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 0) {
        throw PolonioError(ErrorKind::Runtime, "db_close: expected 0 arguments", interp.path(), loc);
    }
    auto* conn = interp.db_connection();
    if (!conn || !conn->is_open()) {
        throw PolonioError(ErrorKind::Runtime, "database not connected", interp.path(), loc);
    }
    conn->close();
    return Value();
}

Value builtin_db_query(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() < 1 || args.size() > 2) {
        throw PolonioError(ErrorKind::Runtime, "db_query: expected 1 or 2 arguments", interp.path(), loc);
    }
    const Value& sql_value = ensure_arg("db_query", 0, args, interp, loc);
    std::string sql = require_string_value("db_query", sql_value, interp, loc, "sql must be string");
    Value::ArrayPtr params;
    if (args.size() == 2) {
        const Value& params_value = ensure_arg("db_query", 1, args, interp, loc);
        params = require_array_value("db_query", params_value, interp, loc, "params must be array");
    }
    sqlite3* db = require_db_handle(interp, "db_query", loc);
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = sqlite3_errmsg(db);
        throw PolonioError(ErrorKind::Runtime,
                           "db_query: sqlite prepare failed: " + message,
                           interp.path(),
                           loc);
    }
    SQLiteStatementPtr stmt(raw_stmt);
    bind_sqlite_parameters(stmt.get(), params, "db_query", interp, loc);
    Value::Array rows;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        rows.emplace_back(make_row_value(stmt.get()));
    }
    if (rc != SQLITE_DONE) {
        std::string message = sqlite3_errmsg(db);
        throw PolonioError(ErrorKind::Runtime,
                           "db_query: sqlite step failed: " + message,
                           interp.path(),
                           loc);
    }
    return Value(std::move(rows));
}

Value builtin_db_exec(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() < 1 || args.size() > 2) {
        throw PolonioError(ErrorKind::Runtime, "db_exec: expected 1 or 2 arguments", interp.path(), loc);
    }
    const Value& sql_value = ensure_arg("db_exec", 0, args, interp, loc);
    std::string sql = require_string_value("db_exec", sql_value, interp, loc, "sql must be string");
    Value::ArrayPtr params;
    if (args.size() == 2) {
        const Value& params_value = ensure_arg("db_exec", 1, args, interp, loc);
        params = require_array_value("db_exec", params_value, interp, loc, "params must be array");
    }
    sqlite3* db = require_db_handle(interp, "db_exec", loc);
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = sqlite3_errmsg(db);
        throw PolonioError(ErrorKind::Runtime,
                           "db_exec: sqlite prepare failed: " + message,
                           interp.path(),
                           loc);
    }
    SQLiteStatementPtr stmt(raw_stmt);
    bind_sqlite_parameters(stmt.get(), params, "db_exec", interp, loc);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        // ignore rows for exec
    }
    if (rc != SQLITE_DONE) {
        std::string message = sqlite3_errmsg(db);
        throw PolonioError(ErrorKind::Runtime,
                           "db_exec: sqlite step failed: " + message,
                           interp.path(),
                           loc);
    }
    return Value(static_cast<double>(sqlite3_changes(db)));
}

Value builtin_db_last_insert_id(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    if (args.size() != 0) {
        throw PolonioError(ErrorKind::Runtime, "db_last_insert_id: expected 0 arguments", interp.path(), loc);
    }
    sqlite3* db = require_db_handle(interp, "db_last_insert_id", loc);
    return Value(static_cast<double>(sqlite3_last_insert_rowid(db)));
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
    env.set_local("file_read", Value(BuiltinFunction{"file_read", builtin_file_read}));
    env.set_local("file_write", Value(BuiltinFunction{"file_write", builtin_file_write}));
    env.set_local("file_append", Value(BuiltinFunction{"file_append", builtin_file_append}));
    env.set_local("file_exists", Value(BuiltinFunction{"file_exists", builtin_file_exists}));
    env.set_local("file_delete", Value(BuiltinFunction{"file_delete", builtin_file_delete}));
    env.set_local("file_size", Value(BuiltinFunction{"file_size", builtin_file_size}));
    env.set_local("file_modified", Value(BuiltinFunction{"file_modified", builtin_file_modified}));
    env.set_local("dir_create", Value(BuiltinFunction{"dir_create", builtin_dir_create}));
    env.set_local("dir_list", Value(BuiltinFunction{"dir_list", builtin_dir_list}));
    env.set_local("dir_exists", Value(BuiltinFunction{"dir_exists", builtin_dir_exists}));
    env.set_local("db_connect", Value(BuiltinFunction{"db_connect", builtin_db_connect}));
    env.set_local("db_close", Value(BuiltinFunction{"db_close", builtin_db_close}));
    env.set_local("db_query", Value(BuiltinFunction{"db_query", builtin_db_query}));
    env.set_local("db_exec", Value(BuiltinFunction{"db_exec", builtin_db_exec}));
    env.set_local("db_last_insert_id",
                  Value(BuiltinFunction{"db_last_insert_id", builtin_db_last_insert_id}));
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
    env.set_local("session_get", Value(BuiltinFunction{"session_get", builtin_session_get}));
    env.set_local("session_set", Value(BuiltinFunction{"session_set", builtin_session_set}));
    env.set_local("session_unset", Value(BuiltinFunction{"session_unset", builtin_session_unset}));
    env.set_local("session_clear", Value(BuiltinFunction{"session_clear", builtin_session_clear}));
    env.set_local("random_token", Value(BuiltinFunction{"random_token", builtin_random_token}));
    env.set_local("csrf_token", Value(BuiltinFunction{"csrf_token", builtin_csrf_token}));
    env.set_local("csrf_verify", Value(BuiltinFunction{"csrf_verify", builtin_csrf_verify}));
    env.set_local("hash_password", Value(BuiltinFunction{"hash_password", builtin_hash_password}));
    env.set_local("verify_password", Value(BuiltinFunction{"verify_password", builtin_verify_password}));
}

} // namespace polonio
