// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_TEMPLATE_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_TEMPLATE_UTIL_H_

#include <type_traits>
#include <utility>

namespace fidl {
namespace internal {

// Types YesType and NoType are guaranteed such that sizeof(YesType) <
// sizeof(NoType).
typedef char YesType;

struct NoType {
  YesType dummy[2];
};

// A helper template to determine if given type is non-const move-only-type,
template <typename T>
struct IsMoveOnlyType {
    static const bool value =
      !std::is_const<T>::value &&
      std::is_move_constructible<T>::value &&
      std::is_move_assignable<T>::value &&
      !std::is_copy_constructible<T>::value &&
      !std::is_copy_assignable<T>::value;
};

// Returns a reference to |t| when T is not a move-only type.
template <typename T>
typename std::enable_if<!IsMoveOnlyType<T>::value, T>::type& Forward(T& t) {
  return t;
}

// Returns the result of t.Pass() when T is a move-only type.
template <typename T>
typename std::enable_if<IsMoveOnlyType<T>::value, T>::type Forward(T& t) {
  return std::move(t);
}

template <template <typename...> class Template, typename T>
struct IsSpecializationOf : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct IsSpecializationOf<Template, Template<Args...>> : std::true_type {};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_TEMPLATE_UTIL_H_
