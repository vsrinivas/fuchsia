// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_UTILS_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_UTILS_H_

#include <errno.h>

#include <clocale>
#include <string>

#include <fidl/error_reporter.h>
#include <fidl/findings.h>

namespace fidl {
namespace utils {

enum class ParseNumericResult {
    kSuccess,
    kOutOfBounds,
    kMalformed,
};

template <typename NumericType>
ParseNumericResult ParseNumeric(const std::string& input,
                                NumericType* out_value,
                                int base = 0) {
    assert(out_value != nullptr);

    // Set locale to "C" for numeric types, since all strtox() functions are locale-dependent
    setlocale(LC_NUMERIC, "C");

    const char* startptr = input.data();
    if (base == 0 && 2 < input.size() && input[0] == '0' && (input[1] == 'b' || input[1] == 'B')) {
        startptr += 2;
        base = 2;
    }
    char* endptr;
    if constexpr (std::is_unsigned<NumericType>::value) {
        if (input[0] == '-')
            return ParseNumericResult::kOutOfBounds;
        errno = 0;
        unsigned long long value = strtoull(startptr, &endptr, base);
        if (errno != 0)
            return ParseNumericResult::kMalformed;
        if (value > std::numeric_limits<NumericType>::max())
            return ParseNumericResult::kOutOfBounds;
        *out_value = static_cast<NumericType>(value);
    } else if constexpr (std::is_floating_point<NumericType>::value) {
        errno = 0;
        long double value = strtold(startptr, &endptr);
        if (errno != 0)
            return ParseNumericResult::kMalformed;
        if (value > std::numeric_limits<NumericType>::max())
            return ParseNumericResult::kOutOfBounds;
        if (value < std::numeric_limits<NumericType>::lowest())
            return ParseNumericResult::kOutOfBounds;
        *out_value = static_cast<NumericType>(value);
    } else {
        errno = 0;
        long long value = strtoll(startptr, &endptr, base);
        if (errno != 0)
            return ParseNumericResult::kMalformed;
        if (value > std::numeric_limits<NumericType>::max())
            return ParseNumericResult::kOutOfBounds;
        if (value < std::numeric_limits<NumericType>::lowest())
            return ParseNumericResult::kOutOfBounds;
        *out_value = static_cast<NumericType>(value);
    }
    if (endptr != (input.c_str() + input.size()))
        return ParseNumericResult::kMalformed;
    return ParseNumericResult::kSuccess;
}

bool ends_with_underscore(const std::string& str);
bool has_adjacent_underscores(const std::string& str);

std::vector<std::string> id_to_words(const std::string& str);

bool is_konstant_case(const std::string& str);
bool is_lower_no_separator_case(const std::string& str);
bool is_lower_snake_case(const std::string& str);
bool is_upper_snake_case(const std::string& str);
bool is_lower_camel_case(const std::string& str);
bool is_upper_camel_case(const std::string& str);

std::string strip_konstant_k(const std::string& str);
std::string to_lower_no_separator_case(const std::string& str);
std::string to_lower_snake_case(const std::string& str);
std::string to_upper_snake_case(const std::string& str);
std::string to_lower_camel_case(const std::string& str);
std::string to_upper_camel_case(const std::string& str);

// Used by fidl-lint main() and for testing, this generates the linter error
// messages in the format required for the fidl::ErrorReporter.
void WriteFindingsToErrorReporter(
    const Findings& findings, ErrorReporter* error_reporter);

} // namespace utils
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_UTILS_H_
