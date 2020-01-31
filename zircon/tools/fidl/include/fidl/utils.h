// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_UTILS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_UTILS_H_

#include <errno.h>

#include <clocale>
#include <set>
#include <string>

#include <fidl/error_reporter.h>
#include <fidl/findings.h>

namespace fidl {
namespace utils {

constexpr char kWhitespaceChars[] = " \t\n\v\f\r";
constexpr char kWhitespaceNoNewlineChars[] = " \t\v\f\r";

inline bool IsWhitespace(char ch) { return ch != '\0' && strchr(kWhitespaceChars, ch) != nullptr; }

inline bool IsWhitespaceNoNewline(char ch) {
  return ch != '\0' && strchr(kWhitespaceNoNewlineChars, ch) != nullptr;
}

// Returns true if the view has anything other than whitespace
inline bool IsBlank(std::string_view view) {
  return view.find_first_not_of(kWhitespaceChars) == std::string::npos;
}

inline bool LineFromOffsetIsBlank(const std::string& str, size_t offset) {
  for (size_t i = offset; i < str.size() && str[i] != '\n'; i++) {
    if (!IsWhitespaceNoNewline(str[i])) {
      return false;
    }
  }
  return true;
}

inline bool FirstLineIsBlank(const std::string& str) { return LineFromOffsetIsBlank(str, 0); }

inline bool LineFromOffsetIsRegularComment(std::string_view view, size_t offset) {
  size_t i = offset;
  if ((i + 1 < view.size()) && view[i] == '/' && view[i + 1] == '/') {
    // Doc comments, which start with three slashes, should not
    // be treated as comments since they get internally converted
    // to attributes. But comments that start with more than three
    // slashes are not converted to doc comment attributes.
    if (view.size() == 2) {
      return true;
    } else {
      return (i + 2 == view.size()) || (view[i + 2] != '/') ||
             ((i + 3 < view.size()) && (view[i + 3] == '/'));
    }
  } else {
    return false;
  }
}

inline bool FirstLineIsRegularComment(std::string_view view) {
  return LineFromOffsetIsRegularComment(view, 0);
}

enum class ParseNumericResult {
  kSuccess,
  kOutOfBounds,
  kMalformed,
};

template <typename NumericType>
ParseNumericResult ParseNumeric(const std::string& input, NumericType* out_value, int base = 0) {
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

// Split the identifier into words, excluding words in the |stop_words| set.
std::vector<std::string> id_to_words(const std::string& str,
                                     const std::set<std::string> stop_words);

bool is_konstant_case(const std::string& str);
bool is_lower_no_separator_case(const std::string& str);
bool is_lower_snake_case(const std::string& str);
bool is_upper_snake_case(const std::string& str);
bool is_lower_camel_case(const std::string& str);
bool is_upper_camel_case(const std::string& str);

std::string strip_konstant_k(const std::string& str);
std::string to_konstant_case(const std::string& str);
std::string to_lower_no_separator_case(const std::string& str);
std::string to_lower_snake_case(const std::string& str);
std::string to_upper_snake_case(const std::string& str);
std::string to_lower_camel_case(const std::string& str);
std::string to_upper_camel_case(const std::string& str);

// Used by fidl-lint WriteFindingsToErrorReporter, and for testing,
// this generates the linter error message string in the format
// required for the fidl::ErrorReporter.
void PrintFinding(std::ostream& os, const Finding& finding);

// Used by fidl-lint main() and for testing, this generates the linter error
// messages for a list of findings.
void WriteFindingsToErrorReporter(const Findings& findings, ErrorReporter* error_reporter);

// Gets a string with the original file contents, and a string with the
// formatted file, and makes sure that the only difference is in the whitespace.
// Used by the formatter to make sure that formatting was not destructive.
bool OnlyWhitespaceChanged(const std::string& unformatted_input,
                           const std::string& formatted_output);

}  // namespace utils
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_UTILS_H_
