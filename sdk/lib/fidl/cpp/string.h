// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_STRING_H_
#define LIB_FIDL_CPP_STRING_H_

#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/assert.h>

#include <iosfwd>
#include <string>
#include <utility>

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/traits.h"

namespace fidl {

class StringPtr final : public cpp17::optional<std::string> {
 public:
  constexpr StringPtr() = default;

  constexpr StringPtr(cpp17::nullopt_t) noexcept {}
  // Deprecated in favor of cpp17::nullopt_t.
  constexpr StringPtr(std::nullptr_t) noexcept {}

  StringPtr(const StringPtr&) = default;
  StringPtr& operator=(const StringPtr&) = default;

  StringPtr(StringPtr&&) = default;
  StringPtr& operator=(StringPtr&&) = default;

  // Move construct and move assignment from the value type
  constexpr StringPtr(std::string&& value) : cpp17::optional<std::string>(std::move(value)) {}
  constexpr StringPtr& operator=(std::string&& value) {
    cpp17::optional<std::string>::operator=(std::move(value));
    return *this;
  }

  // Copy construct and copy assignment from the value type
  constexpr StringPtr(const std::string& value) : cpp17::optional<std::string>(value) {}
  constexpr StringPtr& operator=(const std::string& value) {
    cpp17::optional<std::string>::operator=(value);
    return *this;
  }

  // Construct from string literals
  template <size_t N>
  constexpr StringPtr(const char (&literal)[N]) : cpp17::optional<std::string>(literal) {}
  template <size_t N>
  constexpr StringPtr& operator=(const char (&literal)[N]) {
    cpp17::optional<std::string>::operator=(literal);
    return *this;
  }

  // Construct from string pointers
  StringPtr(const char* value) : cpp17::optional<std::string>(value) {}
  StringPtr(const char* value, size_t size)
      : cpp17::optional<std::string>(std::string(value, size)) {}
  StringPtr& operator=(const char* value) {
    cpp17::optional<std::string>::operator=(value);
    return *this;
  }

  // Construct from string views.
  StringPtr(cpp17::string_view value)
      : cpp17::optional<std::string>(std::string(value.data(), value.size())) {}
  StringPtr& operator=(cpp17::string_view value) {
    cpp17::optional<std::string>::operator=(std::string(value.data(), value.size()));
    return *this;
  }

  // Override unchecked accessors with versions that check.
  constexpr std::string* operator->() {
    if (!cpp17::optional<std::string>::has_value()) {
      __builtin_trap();
    }
    return cpp17::optional<std::string>::operator->();
  }
  constexpr const std::string* operator->() const {
    if (!cpp17::optional<std::string>::has_value()) {
      __builtin_trap();
    }
    return cpp17::optional<std::string>::operator->();
  }

  // Destructor.
  ~StringPtr() = default;
};

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

inline std::ostream& operator<<(std::ostream& out, const StringPtr& str) {
  return out << str.value_or("");
}

template <>
struct CodingTraits<::std::string> {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::string* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    const size_t size = value->size();
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = size;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(size);
    char* payload = encoder->template GetPtr<char>(base);
    memcpy(payload, value->data(), size);
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
  static void Encode(EncoderImpl* encoder, StringPtr* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    if (value->has_value()) {
      ::fidl::CodingTraits<std::string>::Encode(encoder, &value->value(), offset, cpp17::nullopt);
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

inline std::ostream& operator<<(std::ostream& out, const cpp17::optional<std::string>& str) {
  return out << str.value_or("");
}

}  // namespace fit

#endif  // LIB_FIDL_CPP_STRING_H_
