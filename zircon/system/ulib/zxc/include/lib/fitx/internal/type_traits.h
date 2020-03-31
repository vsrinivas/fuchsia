// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FITX_INTERNAL_TYPE_TRAITS_H_
#define LIB_FITX_INTERNAL_TYPE_TRAITS_H_

#include <type_traits>
#include <utility>

// Internal C++14 polyfill for C++17 traits and utilities.

namespace fitx {
namespace internal {

#if __cplusplus >= 201703L && !defined(ZXC_TYPE_TRAITS_INTERNAL_TEST)

using std::void_t;

using std::conjunction;
using std::conjunction_v;

using std::disjunction;
using std::disjunction_v;

using std::negation;
using std::negation_v;

using std::bool_constant;

#else

template <typename... T>
struct make_void {
  typedef void type;
};
template <typename... T>
using void_t = typename make_void<T...>::type;

template <typename... Ts>
struct conjunction : std::true_type {};
template <typename T>
struct conjunction<T> : T {};
template <typename First, typename... Rest>
struct conjunction<First, Rest...>
    : std::conditional_t<bool(First::value), conjunction<Rest...>, First> {};

template <typename... Ts>
constexpr bool conjunction_v = conjunction<Ts...>::value;

template <typename... Ts>
struct disjunction : std::false_type {};
template <typename T>
struct disjunction<T> : T {};
template <typename First, typename... Rest>
struct disjunction<First, Rest...>
    : std::conditional_t<bool(First::value), First, disjunction<Rest...>> {};

template <typename... Ts>
constexpr bool disjunction_v = disjunction<Ts...>::value;

template <typename T>
struct negation : std::integral_constant<bool, !bool(T::value)> {};

template <typename T>
constexpr bool negation_v = negation<T>::value;

template <bool B>
using bool_constant = std::integral_constant<bool, B>;

#endif

}  // namespace internal
}  // namespace fitx

#endif  // LIB_FITX_INTERNAL_TYPE_TRAITS_H_
