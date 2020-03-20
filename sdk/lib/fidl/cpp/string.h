// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_STRING_H_
#define LIB_FIDL_CPP_STRING_H_

#include <lib/fidl/cpp/builder.h>
#include <lib/fit/optional.h>
#include <zircon/assert.h>

#include <iosfwd>
#include <string>
#include <utility>

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/traits.h"
#include "lib/fidl/cpp/transition.h"

namespace fidl {

#if defined(FIDL_USE_FIT_OPTIONAL)

class StringPtr final : public fit::optional<std::string> {
 public:
  constexpr StringPtr() = default;

  constexpr StringPtr(fit::nullopt_t) noexcept {}
  FIDL_FIT_OPTIONAL_DEPRECATED("Use fit::nullopt instead of nullptr")
  constexpr StringPtr(std::nullptr_t) noexcept {}

  StringPtr(const StringPtr&) = default;
  StringPtr& operator=(const StringPtr&) = default;

  StringPtr(StringPtr&&) = default;
  StringPtr& operator=(StringPtr&&) = default;

  // Move construct and move assignment from the value type
  constexpr StringPtr(std::string&& value) : fit::optional<std::string>(std::move(value)) {}
  constexpr StringPtr& operator=(std::string&& value) {
    fit::optional<std::string>::operator=(std::move(value));
    return *this;
  }

  // Copy construct and copy assignment from the value type
  constexpr StringPtr(const std::string& value) : fit::optional<std::string>(value) {}
  constexpr StringPtr& operator=(const std::string& value) {
    fit::optional<std::string>::operator=(value);
    return *this;
  }

  // Construct from string literals
  template <size_t N>
  constexpr StringPtr(const char (&literal)[N]) : fit::optional<std::string>(literal) {}
  template <size_t N>
  constexpr StringPtr& operator=(const char (&literal)[N]) {
    fit::optional<std::string>::operator=(literal);
    return *this;
  }

  // Construct from string pointers
  StringPtr(const char* value) : fit::optional<std::string>(value) {}
  StringPtr& operator=(const char* value) {
    fit::optional<std::string>::operator=(value);
    return *this;
  }

  // Override unchecked accessors with versions that check.
  constexpr std::string* operator->() {
    if (!fit::optional<std::string>::has_value()) {
      __builtin_trap();
    }
    return fit::optional<std::string>::operator->();
  }
  constexpr const std::string* operator->() const {
    if (!fit::optional<std::string>::has_value()) {
      __builtin_trap();
    }
    return fit::optional<std::string>::operator->();
  }

  // Destructor.
  ~StringPtr() = default;
};

#else

// A representation of a FIDL string that owns the memory for the string.
//
// A StringPtr has three states: (1) null, (2) empty, (3) contains a string. In
// the second state, operations that return an std::string return the empty
// std::string. The null and empty states can be distinguished using the
// |is_null| and |operator bool| methods.
class StringPtr final {
 public:
  StringPtr() = default;
  StringPtr(const StringPtr& other) = default;
  StringPtr(StringPtr&& other) noexcept = default;
  ~StringPtr() = default;

  StringPtr& operator=(const StringPtr&) = default;
  StringPtr& operator=(StringPtr&& other) = default;

  StringPtr(std::string str) : str_(std::move(str)), is_null_if_empty_(false) {}
  StringPtr(const char* str)
      : str_(str ? std::string(str) : std::string()), is_null_if_empty_(!str) {}
  FIDL_FIT_OPTIONAL_DEPRECATED("use StringPtr(std::string(bytes, length))")
  StringPtr(const char* str, size_t length)
      : str_(str ? std::string(str, length) : std::string()), is_null_if_empty_(!str) {}

  // Accesses the underlying std::string object.
  FIDL_FIT_OPTIONAL_DEPRECATED("use value_or(\"\")")
  const std::string& get() const { return str_; }

  // Accesses the underlying std::string object.
  const std::string& value() const { return str_; }
  std::string& value() { return str_; }

  std::string value_or(std::string&& default_value) const& {
    if (has_value()) {
      return str_;
    } else {
      return std::move(default_value);
    }
  }

  std::string value_or(std::string&& default_value) && {
    if (has_value()) {
      return std::move(str_);
    } else {
      return std::move(default_value);
    }
  }

  // Stores the given std::string in this StringPtr.
  //
  // After this method returns, the StringPtr is non-null.
  FIDL_FIT_OPTIONAL_DEPRECATED("use assignment")
  void reset(std::string str) {
    str_ = std::move(str);
    is_null_if_empty_ = false;
  }

  // Causes this StringPtr to become null.
  void reset() {
    str_.clear();
    is_null_if_empty_ = true;
  }

  void swap(StringPtr& other) {
    using std::swap;
    swap(str_, other.str_);
    swap(is_null_if_empty_, other.is_null_if_empty_);
  }

  // Whether this StringPtr is null.
  //
  // The null state is separate from the empty state.
  FIDL_FIT_OPTIONAL_DEPRECATED("use !has_value()")
  bool is_null() const { return is_null_if_empty_ && str_.empty(); }

  bool has_value() const { return !(is_null_if_empty_ && str_.empty()); }

  // Tests as true if non-null, false if null.
  explicit operator bool() const { return has_value(); }

  // Provides access to the underlying std::string.
  std::string* operator->() { return &str_; }
  const std::string* operator->() const { return &str_; }

  // Provides access to the underlying std::string.
  const std::string& operator*() const { return str_; }

  FIDL_FIT_OPTIONAL_DEPRECATED("use value_or(\"\")")
  operator const std::string &() const { return str_; }

 private:
  std::string str_;
  bool is_null_if_empty_ = true;
};

#endif

template <>
struct Equality<StringPtr> {
  bool operator()(const StringPtr& lhs, const StringPtr& rhs) const {
    if (!lhs.has_value()) {
      return !rhs.has_value();
    }
    return rhs.has_value() && lhs.value() == rhs.value();
  }
};

inline bool operator==(const StringPtr& lhs, const StringPtr& rhs) {
  return ::fidl::Equality<StringPtr>{}(lhs, rhs);
}

inline bool operator==(const char* lhs, const StringPtr& rhs) {
  if (lhs == nullptr) {
    return !rhs.has_value();
  }
  return rhs.has_value() && lhs == rhs.value();
}

inline bool operator==(const StringPtr& lhs, const char* rhs) {
  if (!lhs.has_value()) {
    return rhs == nullptr;
  }
  return rhs != nullptr && lhs.value() == rhs;
}

inline bool operator!=(const StringPtr& lhs, const StringPtr& rhs) { return !(lhs == rhs); }

inline bool operator!=(const char* lhs, const StringPtr& rhs) { return !(lhs == rhs); }

inline bool operator!=(const StringPtr& lhs, const char* rhs) { return !(lhs == rhs); }

inline bool operator<(const StringPtr& lhs, const StringPtr& rhs) {
  if (!lhs.has_value() || !rhs.has_value()) {
    return rhs.has_value();
  }
  return *lhs < *rhs;
}

inline bool operator<(const char* lhs, const StringPtr& rhs) {
  if (lhs == nullptr || !rhs.has_value()) {
    return rhs.has_value();
  }
  return lhs < *rhs;
}

inline bool operator<(const StringPtr& lhs, const char* rhs) {
  if (!lhs.has_value() || rhs == nullptr) {
    return rhs != nullptr;
  }
  return *lhs < rhs;
}

inline bool operator>(const StringPtr& lhs, const StringPtr& rhs) {
  if (!lhs.has_value() || !rhs.has_value()) {
    return !!lhs.has_value();
  }
  return *lhs > *rhs;
}

inline bool operator>(const char* lhs, const StringPtr& rhs) {
  if (lhs == nullptr || !rhs.has_value()) {
    return lhs != nullptr;
  }
  return lhs > *rhs;
}

inline bool operator>(const StringPtr& lhs, const char* rhs) {
  if (!lhs.has_value() || rhs == nullptr) {
    return lhs != nullptr;
  }
  return *lhs > rhs;
}

inline bool operator<=(const StringPtr& lhs, const StringPtr& rhs) { return !(lhs > rhs); }

inline bool operator<=(const char* lhs, const StringPtr& rhs) { return !(lhs > rhs); }

inline bool operator<=(const StringPtr& lhs, const char* rhs) { return !(lhs > rhs); }

inline bool operator>=(const StringPtr& lhs, const StringPtr& rhs) { return !(lhs < rhs); }

inline bool operator>=(const char* lhs, const StringPtr& rhs) { return !(lhs < rhs); }

inline bool operator>=(const StringPtr& lhs, const char* rhs) { return !(lhs < rhs); }

#if defined(FIDL_USE_FIT_OPTIONAL)
FIDL_FIT_OPTIONAL_DEPRECATED("Use value_or(\"\")")
#endif
inline std::ostream& operator<<(std::ostream& out, const StringPtr& str) {
  return out << str.value_or("");
}

template <>
struct CodingTraits<::std::string> {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::string* value, size_t offset) {
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = value->size();
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(value->size());
    char* payload = encoder->template GetPtr<char>(base);
    memcpy(payload, value->data(), value->size());
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::string* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    ZX_ASSERT(string->data != nullptr);
    *value = std::string(string->data, string->size);
  }
};

template <>
struct CodingTraits<StringPtr> {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, StringPtr* value, size_t offset) {
    if (value->has_value()) {
      ::fidl::CodingTraits<std::string>::Encode(encoder, &value->value(), offset);
    } else {
      fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
      string->size = 0u;
      string->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
    }
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, StringPtr* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    if (string->data) {
      std::string string_value;
      ::fidl::CodingTraits<std::string>::Decode(decoder, &string_value, offset);
      (*value) = std::move(string_value);
    } else {
      value->reset();
    }
  }
};

}  // namespace fidl

namespace fit {

inline std::ostream& operator<<(std::ostream& out, const fit::optional<std::string>& str) {
  return out << str.value_or("");
}

}  // namespace fit

#endif  // LIB_FIDL_CPP_STRING_H_
