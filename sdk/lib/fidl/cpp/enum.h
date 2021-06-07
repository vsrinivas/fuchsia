// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_ENUM_H_
#define LIB_FIDL_CPP_ENUM_H_

#include <type_traits>

namespace fidl {

// Converts an enum value to its underlying type.
template <typename TEnum>
constexpr auto ToUnderlying(TEnum value) -> typename std::underlying_type<TEnum>::type {
  return static_cast<typename std::underlying_type<TEnum>::type>(value);
}

namespace internal {

template <typename EnumT, typename UnderlyingT>
class FlexibleEnumValue {
 public:
  constexpr FlexibleEnumValue(const FlexibleEnumValue&) noexcept = default;
  constexpr FlexibleEnumValue(FlexibleEnumValue&&) noexcept = default;
  constexpr FlexibleEnumValue& operator=(const FlexibleEnumValue&) noexcept = default;
  constexpr FlexibleEnumValue& operator=(FlexibleEnumValue&&) noexcept = default;
  constexpr operator UnderlyingT() const { return value_; }
  constexpr bool IsUnknown() const {
    EnumT e{value_};
    return e.IsUnknown();
  }

 private:
  constexpr FlexibleEnumValue() = delete;
  friend EnumT;
  constexpr explicit FlexibleEnumValue(UnderlyingT value) noexcept : value_(value) {}

  UnderlyingT value_;
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_ENUM_H_
