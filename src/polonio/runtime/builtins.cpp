#include "polonio/runtime/builtins.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/output.h"
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

Value builtin_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_tostring(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_nl2br(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
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
Value builtin_date_parts(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_date_format(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_count(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_push(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_pop(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_join(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_range(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_keys(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_has_key(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_get(Interpreter& interp, const std::vector<Value>& args, const Location& loc);
Value builtin_set(Interpreter& interp, const std::vector<Value>& args, const Location& loc);

Value builtin_type(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("type", 0, args, interp, loc);
    return Value(value.type_name());
}

Value builtin_tostring(Interpreter& interp, const std::vector<Value>& args, const Location& loc) {
    Value value = ensure_arg("tostring", 0, args, interp, loc);
    return Value(OutputBuffer::value_to_string(value));
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

} // namespace

void install_builtins(Env& env) {
    env.set_local("type", Value(BuiltinFunction{"type", builtin_type}));
    env.set_local("tostring", Value(BuiltinFunction{"tostring", builtin_tostring}));
    env.set_local("nl2br", Value(BuiltinFunction{"nl2br", builtin_nl2br}));
    env.set_local("len", Value(BuiltinFunction{"len", builtin_len}));
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
    env.set_local("join", Value(BuiltinFunction{"join", builtin_join}));
    env.set_local("range", Value(BuiltinFunction{"range", builtin_range}));
    env.set_local("keys", Value(BuiltinFunction{"keys", builtin_keys}));
    env.set_local("has_key", Value(BuiltinFunction{"has_key", builtin_has_key}));
    env.set_local("get", Value(BuiltinFunction{"get", builtin_get}));
    env.set_local("set", Value(BuiltinFunction{"set", builtin_set}));
    env.set_local("abs", Value(BuiltinFunction{"abs", builtin_abs}));
    env.set_local("floor", Value(BuiltinFunction{"floor", builtin_floor}));
    env.set_local("ceil", Value(BuiltinFunction{"ceil", builtin_ceil}));
    env.set_local("round", Value(BuiltinFunction{"round", builtin_round}));
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
    env.set_local("date_parts", Value(BuiltinFunction{"date_parts", builtin_date_parts}));
    env.set_local("date_format", Value(BuiltinFunction{"date_format", builtin_date_format}));
}

} // namespace polonio
