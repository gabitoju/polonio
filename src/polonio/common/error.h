#pragma once

#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

#include "polonio/common/location.h"

namespace polonio {

enum class ErrorCategory {
    Source,
    Lex,
    Parse,
    Runtime,
    Capability,
    Resource,
    Internal,
    // Compatibility spelling for pre-RFC-0004 callers.
    IO = Source,
};

// RFC 0006 eligibility for a future restricted recovery mechanism. It does
// not make an error catchable in v1 and is deliberately derived only from the
// immutable structural category.
enum class Recoverability {
    Never,
    Operational,
};

Recoverability recoverability_for(ErrorCategory category) noexcept;

// RFC 0005 models builtin failure facts without adding public error codes.
enum class BuiltinFailureReason {
    Arity, Type, Value, Shape, UnsupportedValue, Context, Configuration,
    Resource, Operation,
};

using ErrorKind = ErrorCategory;

struct ErrorDetails {
    std::string operation;
    std::string function_name;
    std::optional<std::size_t> argument_index;
    std::string capability;
    std::string resource;
    std::vector<std::string> include_chain;
    std::string host_detail;
    std::string canonical_function_name;
    std::optional<BuiltinFailureReason> builtin_reason;
    std::optional<std::size_t> expected_arity_min;
    std::optional<std::size_t> expected_arity_max;
    std::optional<std::size_t> actual_arity;
    std::string expected_type;
    std::string actual_type;
    std::string expected_value;
    std::string actual_value_summary;
    std::string option_name;
    std::string configuration_name;
};

class PolonioError : public std::runtime_error {
public:
    PolonioError(ErrorCategory category,
                 std::string message,
                 std::string path = std::string());
    PolonioError(ErrorCategory category,
                 std::string message,
                 std::string path,
                 Location location,
                 ErrorDetails details = {});

    ErrorCategory category() const noexcept { return category_; }
    Recoverability recoverability() const noexcept { return recoverability_for(category_); }
    ErrorKind kind() const noexcept { return category_; }
    const std::string& path() const noexcept { return path_; }
    const Location& location() const noexcept { return location_; }
    bool has_location() const noexcept { return has_location_; }
    const std::string& message() const noexcept { return message_; }
    const ErrorDetails& details() const noexcept { return details_; }
    ErrorDetails& mutable_details() noexcept { return details_; }
    void set_category(ErrorCategory category) noexcept { category_ = category; }
    void add_include_frame(std::string frame);

    std::string format() const;

private:
    ErrorCategory category_;
    std::string message_;
    std::string path_;
    Location location_;
    bool has_location_ = false;
    ErrorDetails details_;
};

} // namespace polonio
