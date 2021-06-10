// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_MESSAGE_H_
#define ZXTEST_BASE_MESSAGE_H_

#include <zircon/status.h>

#include <array>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <zxtest/base/types.h>

namespace zxtest {

namespace internal {

inline std::string_view ToStringView(std::string_view str) { return str; }

// We avoid calling std::string_view's constructor of a single `const char*`
// argument, as that will call std::char_traits<char>::length() which may not
// support nullptrs. Similarly we avoid calling strlen on a nullptr, as it
// varies by implementation whether that is supported.
inline std::string_view ToStringView(const char* str) { return {str, str ? strlen(str) : 0}; }

inline std::string_view ToStringView(char* str) {
  return ToStringView(static_cast<const char*>(str));
}

// Else, default to assuming that std::data will yield a C string.
template <typename Stringlike>
inline std::string_view ToStringView(const Stringlike& str) {
  static_assert(
      std::is_convertible_v<decltype(std::data(std::declval<Stringlike&>())), const char*>);
  return ToStringView(std::data(str));
}

}  // namespace internal

// Helper class for handling the error information, plus some logic for printing the correct error
// messages.
class Message {
 public:
  Message() = delete;
  Message(const fbl::String& desc, const SourceLocation& location);
  Message(const Message&) = delete;
  Message(Message&&) noexcept;
  ~Message();

  Message& operator=(const Message&) = delete;
  Message& operator=(Message&&) = delete;

  // Returns the position at which the message was sent.
  const SourceLocation& location() const { return location_; }

  // Returns the text of the message.
  const fbl::String& text() const { return text_; }

 private:
  fbl::String text_;

  SourceLocation location_;
};

// Helper functions used on assertion reporting contexts.
namespace internal {
// Returns a string with the Hex representation of the contents of the buffer pointed by ptr. If
// |ptr| is nullptr, returns "<nullptr>". If |size| is 0 returns <empty>.
fbl::String ToHex(const void* ptr, size_t size);

// It's not necessarily safe to do pointer arithmetic on volatiles because of alignment issues, so
// just print whether the pointer is nullptr/empty/normal.
fbl::String PrintVolatile(volatile const void* ptr, size_t size);

}  // namespace internal

// Specializations exist for primitive types, pointers and |fbl::String|.
template <typename T>
fbl::String PrintValue(const T& value) {
  // TODO(gevalentino): By default generate a hex representation of the memory contents of value.
  return internal::ToHex(&value, sizeof(value));
}

template <typename T>
fbl::String PrintValue(volatile const T& value) {
  return internal::PrintVolatile(&value, sizeof(value));
}

// For pointers just print the address.
template <typename T>
fbl::String PrintValue(const T* value) {
  if (value == nullptr) {
    return "<nullptr>";
  }
  return fbl::StringPrintf("%p", static_cast<const void*>(value));
}

// Template Specialization for integers, floating point, char pointers, and strings.
template <>
fbl::String PrintValue(const int32_t& value);
template <>
fbl::String PrintValue(const uint32_t& value);
template <>
fbl::String PrintValue(const int64_t& value);
template <>
fbl::String PrintValue(const uint64_t& value);
template <>
fbl::String PrintValue(const float& value);
template <>
fbl::String PrintValue(const double& value);
template <>
fbl::String PrintValue(const char* value);
template <>
fbl::String PrintValue(const std::string& value);
template <>
fbl::String PrintValue(const fbl::String& value);

// Print a string form of the status, can't be a specialization of PrintValue because zx_status_t is
// a uint32_t.
fbl::String PrintStatus(zx_status_t status);

// For tuples, recursively print the individual components.
template <typename... Ts>
fbl::String PrintValue(const std::tuple<Ts...>& value) {
  const auto strings = std::apply(
      [&](auto&&... elems) {
        return std::array<fbl::String, sizeof...(Ts)>{
            PrintValue(std::forward<decltype(elems)>(elems))...};
      },
      value);

  // Total size of all string representations, plus:
  // * 1 for the opening "{".
  // * 1 for each " " between representations, plus a "," if it is not the last representation.
  // * 2 for the closing " }".
  size_t total_size = 0;
  for (const auto& s : strings) {
    total_size += s.size();
  }
  total_size += 1 + (2 * strings.size() - 1) + 2;

  char buffer[total_size];
  size_t current = 0;
  buffer[current++] = '{';
  for (size_t index = 0; index < strings.size(); ++index) {
    buffer[current++] = ' ';
    std::memcpy(buffer + current, strings[index].data(), strings[index].size());
    current += strings[index].size();
    if (index + 1 < strings.size()) {
      buffer[current++] = ',';
    }
  }
  buffer[current++] = ' ';
  buffer[current++] = '}';
  return fbl::String(buffer, current);
}

template <typename StringTypeA, typename StringTypeB>
inline bool StrCmp(StringTypeA&& actual, StringTypeB&& expected) {
  std::string_view actual_sv = internal::ToStringView(actual);
  std::string_view expected_sv = internal::ToStringView(expected);
  return actual_sv == expected_sv;
}

template <typename StringTypeA, typename StringTypeB>
inline bool StrContain(StringTypeA&& str, StringTypeB&& substr) {
  std::string_view str_view = internal::ToStringView(str);
  std::string_view substr_view = internal::ToStringView(substr);
  return str_view.find(substr_view) != std::string_view::npos;
}

}  // namespace zxtest

#endif  // ZXTEST_BASE_MESSAGE_H_
