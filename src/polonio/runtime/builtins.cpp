#include "polonio/runtime/builtins.h"

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
}

} // namespace polonio
