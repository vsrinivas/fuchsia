// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_STRING_H_
#define LIB_FIDL_CPP_BINDINGS_STRING_H_

#include <iosfwd>
#include <string>

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/type_converter.h"
#include "lib/fxl/logging.h"

namespace fidl {

// A UTF-8 encoded character string that can be null. Provides functions that
// are similar to std::string, along with access to the underlying std::string
// object.
class String {
 public:
  // Provide iterator access to the underlying std::string.
  using ConstIterator = typename std::string::const_iterator;
  using Iterator = typename std::string::iterator;

  typedef internal::String_Data Data_;

  String() : is_null_(true) {}
  String(const std::string& str) : value_(str), is_null_(false) {}
  String(const char* chars) : is_null_(!chars) {
    if (chars)
      value_ = chars;
  }
  String(const char* chars, size_t num_chars)
      : value_(chars, num_chars), is_null_(false) {}
  String(const fidl::String& str)
      : value_(str.value_), is_null_(str.is_null_) {}

  template <size_t N>
  String(const char chars[N]) : value_(chars, N - 1), is_null_(false) {}

  template <typename U>
  static String From(const U& other) {
    return TypeConverter<String, U>::Convert(other);
  }

  template <typename U>
  U To() const {
    return TypeConverter<U, String>::Convert(*this);
  }

  String& operator=(const fidl::String& str) {
    value_ = str.value_;
    is_null_ = str.is_null_;
    return *this;
  }
  String& operator=(const std::string& str) {
    value_ = str;
    is_null_ = false;
    return *this;
  }
  String& operator=(const char* chars) {
    is_null_ = !chars;
    if (chars) {
      value_ = chars;
    } else {
      value_.clear();
    }
    return *this;
  }

  void reset() {
    value_.clear();
    is_null_ = true;
  }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return !is_null_; }

  bool is_null() const { return is_null_; }

  size_t size() const { return value_.size(); }

  const char* data() const { return value_.data(); }

  const char& at(size_t offset) const { return value_.at(offset); }
  char& at(size_t offset) { return value_.at(offset); }

  const char& operator[](size_t offset) const { return value_[offset]; }
  char& operator[](size_t offset) { return value_[offset]; }

  const std::string& get() const { return value_; }
  operator const std::string&() const { return value_; }

  void Swap(String* other) {
    std::swap(is_null_, other->is_null_);
    value_.swap(other->value_);
  }

  void Swap(std::string* other) {
    is_null_ = false;
    value_.swap(*other);
  }

  // std::string iterators into the string. The behavior is undefined
  // if the string is null.
  Iterator begin() { return value_.begin(); }
  Iterator end() { return value_.end(); }
  ConstIterator begin() const { return value_.begin(); }
  ConstIterator end() const { return value_.end(); }

 private:
  std::string value_;
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

// TODO(darin): Add similar variants of operator<,<=,>,>=

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
    FXL_DCHECK(input);
    return String(input, N - 1);
  }
};

// Appease MSVC.
template <size_t N>
struct TypeConverter<String, const char[N]> {
  static String Convert(const char input[N]) {
    FXL_DCHECK(input);
    return String(input, N - 1);
  }
};

template <>
struct TypeConverter<String, const char*> {
  // |input| may be null, in which case a null String will be returned.
  static String Convert(const char* input) { return String(input); }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_STRING_H_
