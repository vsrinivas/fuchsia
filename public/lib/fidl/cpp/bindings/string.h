// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_STRING_H_
#define LIB_FIDL_CPP_BINDINGS_STRING_H_

#include <zircon/assert.h>

#include <iosfwd>
#include <string>

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/type_converter.h"

namespace f1dl {

// A UTF-8 encoded character string that can be null. Provides functions that
// are similar to std::string, along with access to the underlying std::string
// object.
class String {
 public:
  typedef internal::String_Data Data_;

  String() : is_null_(true) {}
  String(const f1dl::String& str) : str_(str.str_), is_null_(str.is_null_) {}
  String(const std::string& str) : str_(str), is_null_(false) {}
  String(const char* chars) : is_null_(!chars) {
    if (chars)
      str_ = chars;
  }
  String(const char* chars, size_t num_chars)
      : str_(chars, num_chars), is_null_(false) {}

  const std::string& get() const { return str_; }

  void reset(std::string str) {
    str_ = std::move(str);
    is_null_ = false;
  }

  void reset() {
    str_.clear();
    is_null_ = true;
  }

  void swap(String& other) {
    using std::swap;
    swap(str_, other.str_);
    swap(is_null_, other.is_null_);
  }

  bool is_null() const { return is_null_; }

  explicit operator bool() const { return !is_null_; }

  std::string* operator->() { return &str_; }
  const std::string* operator->() const { return &str_; }

  const std::string& operator*() const { return str_; }

  operator const std::string&() const { return str_; }

 private:
  std::string str_;
  bool is_null_;
};

inline bool operator==(const String& a, const String& b) {
  return a.is_null() == b.is_null() && a.get() == b.get();
}
inline bool operator==(const char* a, const String& b) {
  return !b.is_null() && a == b.get();
}
inline bool operator==(const String& a, const char* b) {
  return !a.is_null() && a.get() == b;
}
inline bool operator!=(const String& a, const String& b) {
  return !(a == b);
}
inline bool operator!=(const char* a, const String& b) {
  return !(a == b);
}
inline bool operator!=(const String& a, const char* b) {
  return !(a == b);
}

// TODO(jeffbrown): Decide whether this should print a sentinel value
// such as "<null>" when formatting null strings.
inline std::ostream& operator<<(std::ostream& out, const String& s) {
  return out << s.get();
}

inline bool operator<(const String& a, const String& b) {
  if (a.is_null())
    return !b.is_null();
  if (b.is_null())
    return false;

  return a.get() < b.get();
}

template <>
struct TypeConverter<String, std::string> {
  static String Convert(const std::string& input) { return String(input); }
};

template <>
struct TypeConverter<std::string, String> {
  static std::string Convert(const String& input) { return input; }
};

template <size_t N>
struct TypeConverter<String, char[N]> {
  static String Convert(const char input[N]) {
    ZX_DEBUG_ASSERT(input);
    return String(input, N - 1);
  }
};

// Appease MSVC.
template <size_t N>
struct TypeConverter<String, const char[N]> {
  static String Convert(const char input[N]) {
    ZX_DEBUG_ASSERT(input);
    return String(input, N - 1);
  }
};

template <>
struct TypeConverter<String, const char*> {
  // |input| may be null, in which case a null String will be returned.
  static String Convert(const char* input) { return String(input); }
};

}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_STRING_H_
