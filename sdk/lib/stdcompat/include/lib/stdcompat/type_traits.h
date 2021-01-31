// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TYPE_TRAITS_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TYPE_TRAITS_H_

#include <tuple>
#include <type_traits>

#include "version.h"

namespace cpp17 {

#if __cpp_lib_void_t >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
using std::void_t;
#else   // Provide std::void_t polyfill.
template <typename... T>
using void_t = void;
#endif  // __cpp_lib_void_t >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#if __cpp_lib_logical_traits >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::conjunction;
using std::conjunction_v;
using std::disjunction;
using std::disjunction_v;
using std::negation;
using std::negation_v;

#else   // Provide polyfills for std::{negation, conjunction, disjunction} and the *_v helpers.

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

// Utility type that negates its truth-like parameter type.
template <typename T>
struct negation : std::integral_constant<bool, !bool(T::value)> {};

template <typename T>
constexpr bool negation_v = negation<T>::value;
#endif  // __cpp_lib_logical_traits >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_TYPE_TRAITS_H_
