// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UTILS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UTILS_H_

#include <errno.h>
#include <zircon/assert.h>

#include <clocale>
#include <cstring>
#include <set>
#include <string>
#include <string_view>

#include <re2/re2.h>

#include "tools/fidl/fidlc/include/fidl/findings.h"

namespace fidl::utils {

// Compares pointers by the values they point to.
template <typename T>
struct PtrCompare {
  bool operator()(const T* left, const T* right) const { return *left < *right; }
};

// Identity function for types, equivalent to C++20 std::type_identity. Often
// used to prevent arguments from participating in template argument deduction.
// https://en.cppreference.com/w/cpp/language/template_argument_deduction#Non-deduced_contexts
//
// We use this to make error reporting more ergonomic. For example:
//
//     template <ErrorId Id, typename Args...>
//     void Fail(const ErrorDef<Id, Args...>& err, const identity_t<Args...>& args);
//
//     ErrorDef<12, const Foo*> ErrOops("...");
//
//     Foo* foo = /* ... */;
//     Fail(ErrOops, foo);
//
// Without the identity wrapper, both `err` and `args` participate in deduction
// for Args, so the compiler complains that `const Foo*` and `Foo*` don't match.
// With the identity wrapper, it deduces Args to be <const Foo*> solely based on
// `err`, and instantiates `Fail(const ErrorDef<12, const Foo*>&, const Foo*&)`.
// From there, `Fail(ErrOops, foo)` works because of implicit conversions.
template <typename T>
struct identity {
  using type = T;
};
template <typename T>
using identity_t = typename identity<T>::type;

// Helper object for creating a callable argument to std::visit by passing in
// lambdas for handling each variant (code comes from
// https://en.cppreference.com/w/cpp/utility/variant/visit)
template <class... Ts>
struct matchers : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
matchers(Ts...) -> matchers<Ts...>;

// Clones a vector of unique_ptr by calling Clone() on each element.
template <typename T>
std::vector<std::unique_ptr<T>> MapClone(const std::vector<std::unique_ptr<T>>& original) {
  std::vector<std::unique_ptr<T>> cloned;
  cloned.reserve(original.size());
  for (const auto& item : original) {
    cloned.push_back(item->Clone());
  }
  return cloned;
}

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

// IsValidLibraryComponent validates individual components of a library
// identifier.
//
// See https://fuchsia.dev/fuchsia-src/reference/fidl/language/language#identifiers
bool IsValidLibraryComponent(std::string_view component);

// IsValidIdentifierComponent validates individual components of an identifier
// (other than a library identifier).
//
// See https://fuchsia.dev/fuchsia-src/reference/fidl/language/language#identifiers
bool IsValidIdentifierComponent(std::string_view component);

// IsValidFullyQualifiedMethodIdentifier validates fully qualified method
// identifiers, i.e. a library identifier, followed by a slash, followed by a
// protocol identifier, a dot, and lastly the method name.
bool IsValidFullyQualifiedMethodIdentifier(std::string_view fq_identifier);

// IsValidDiscoverableName validates a name for use in service discovery. This
// is like a fully qualified identifier, but uses a dot instead of a slash so
// that it can be used as a single component in a filesystem path.
bool IsValidDiscoverableName(std::string_view discoverable_name);

inline bool LineFromOffsetIsBlank(std::string_view str, size_t offset) {
  for (size_t i = offset; i < str.size() && str[i] != '\n'; i++) {
    if (!IsWhitespaceNoNewline(str[i])) {
      return false;
    }
  }
  return true;
}

inline bool FirstLineIsBlank(std::string_view str) { return LineFromOffsetIsBlank(str, 0); }

inline bool LineFromOffsetIsRegularComment(std::string_view view, size_t offset) {
  size_t i = offset;
  if ((i + 1 < view.size()) && view[i] == '/' && view[i + 1] == '/') {
    // Doc comments, which start with three slashes, should not
    // be treated as comments since they get internally converted
    // to attributes. But comments that start with more than three
    // slashes are not converted to doc comment attributes.
    if (view.size() == 2) {
      return true;
    }
    return (i + 2 == view.size()) || (view[i + 2] != '/') ||
           ((i + 3 < view.size()) && (view[i + 3] == '/'));
  }
  return false;
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
ParseNumericResult ParseNumeric(std::string_view input, NumericType* out_value, int base = 0) {
  ZX_ASSERT(out_value != nullptr);

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
  if (endptr != (input.data() + input.size()))
    return ParseNumericResult::kMalformed;
  return ParseNumericResult::kSuccess;
}

bool ends_with_underscore(std::string_view str);
bool has_adjacent_underscores(std::string_view str);

std::vector<std::string> id_to_words(std::string_view str);

// Split the identifier into words, excluding words in the |stop_words| set.
std::vector<std::string> id_to_words(std::string_view str, const std::set<std::string>& stop_words);

bool is_konstant_case(std::string_view str);
bool is_lower_no_separator_case(std::string_view str);
bool is_lower_snake_case(std::string_view str);
bool is_upper_snake_case(std::string_view str);
bool is_lower_camel_case(std::string_view str);
bool is_upper_camel_case(std::string_view str);

std::string strip_string_literal_quotes(std::string_view str);
std::string strip_doc_comment_slashes(std::string_view str);
std::string strip_konstant_k(std::string_view str);
std::string to_konstant_case(std::string_view str);
std::string to_lower_no_separator_case(std::string_view str);
std::string to_lower_snake_case(std::string_view str);
std::string to_upper_snake_case(std::string_view str);
std::string to_lower_camel_case(std::string_view str);
std::string to_upper_camel_case(std::string_view str);

// Decodes 1 to 6 hex digits like "a" or "123" or "FFFFFF".
uint32_t decode_unicode_hex(std::string_view str);

// string_literal_length returns the length of the string
// represented by the provided string literal.
// String literals start and end with double quotes,
// and may contain escape characters.
// For instance, the string Hello\n, i.e.
// the word Hello followed by a newline character,
// is represented as the string literal "Hello\n".
// While the string literal itself has 9 characters,
// the length of the string it represents is 6.
//
// PRECONDITION: str must be a valid string literal.
std::uint32_t string_literal_length(std::string_view str);

// Returns the canonical form of an identifier, used to detect name collisions
// in FIDL libraries. For example, the identifers "FooBar" and "FOO_BAR" collide
// because canonicalize returns "foo_bar" for both.
std::string canonicalize(std::string_view identifier);

std::string StringJoin(const std::vector<std::string_view>& strings, std::string_view separator);

// Used by fidl-lint FormatFindings, and for testing,
// this generates the linter error message string in the format
// required for the fidl::Reporter.
void PrintFinding(std::ostream& os, const Finding& finding);

// Used by fidl-lint main() and for testing, this generates the linter error
// messages for a list of findings.
std::vector<std::string> FormatFindings(const Findings& findings, bool enable_color);

// Gets a string with the original file contents, and a string with the
// formatted file, and makes sure that the only difference is in the whitespace.
// Used by the formatter to make sure that formatting was not destructive.
bool OnlyWhitespaceChanged(std::string_view unformatted_input, std::string_view formatted_output);

}  // namespace fidl::utils

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_UTILS_H_
