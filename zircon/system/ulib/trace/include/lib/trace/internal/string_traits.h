// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header provides functions which make it easier to work generically
// with string-like objects such as fbl::StringPiece, fbl::String, std::string,
// and std::string_view.

#ifndef LIB_TRACE_INTERNAL_STRING_TRAITS_H_
#define LIB_TRACE_INTERNAL_STRING_TRAITS_H_

#include <stddef.h>

#include <type_traits>

namespace trace {
namespace internal {

// Macro for defining a trait that checks if a type T has a method with the
// given name. See fbl/macros.h.
//
// Example:
//
// TRACE_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
//     has_c_str, c_str, const char* (C::*)() const);
#define TRACE_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(trait_name, fn_name, sig) \
  template <typename T>                                                               \
  struct trait_name {                                                                 \
   private:                                                                           \
    template <typename C>                                                             \
    static std::true_type test(decltype(static_cast<sig>(&C::fn_name)));              \
    template <typename C>                                                             \
    static std::false_type test(...);                                                 \
                                                                                      \
   public:                                                                            \
    static constexpr bool value = decltype(test<T>(nullptr))::value;                  \
  };                                                                                  \
  template <typename T>                                                               \
  static /*inline*/ constexpr bool trait_name##_v = trait_name<T>::value

TRACE_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_data, data, const char* (C::*)() const);
TRACE_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_length, length, size_t (C::*)() const);

#undef TRACE_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE

// Gets the character data from a string-like object.
template <typename T>
constexpr const char* GetStringData(const T& value) {
  return value.data();
}

// Gets the length (in characters) of a string-like object.
template <typename T>
constexpr size_t GetStringLength(const T& value) {
  return value.length();
}

// is_string_like_v<T>
//
// Evaluates to true if GetStringData() and GetStringLength() are supported
// instances of type T.
template <typename T>
using is_string_like = std::integral_constant<bool, has_data_v<T> && has_length_v<T>>;

}  // namespace internal
}  // namespace trace

#endif  // LIB_TRACE_INTERNAL_STRING_TRAITS_H_
