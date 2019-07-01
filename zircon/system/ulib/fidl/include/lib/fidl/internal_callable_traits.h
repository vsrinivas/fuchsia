// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_INTERNAL_CALLABLE_TRAITS_H_
#define LIB_FIDL_INTERNAL_CALLABLE_TRAITS_H_

#include <tuple>
#include <type_traits>

namespace fidl {

namespace internal {

// |callable_traits| captures elements of interest from function-like types (functions, function
// pointers, and functors, including lambdas). Due to common usage patterns, const and non-const
// functors are treated identically.
//
// Member types:
//  |args|        - a |std::tuple| that captures the parameter types of the function.
//  |return_type| - the return type of the function.
//  |type|        - the underlying functor or function pointer type. This member is absent if
//                  |callable_traits| are requested for a raw function signature (as opposed to a
//                  function pointer or functor; e.g. |callable_traits<void()>|).
//  |signature|   - the type of the equivalent function.

template <typename T>
struct callable_traits : public callable_traits<decltype(&T::operator())> {};

// Treat mutable call operators the same as const call operators.
//
// It would be equivalent to erase the const instead, but the common case is lambdas, which are
// const, so prefer to nest less deeply for the common const case.
template <typename FunctorType, typename ReturnType, typename... ArgTypes>
struct callable_traits<ReturnType (FunctorType::*)(ArgTypes...)>
    : public callable_traits<ReturnType (FunctorType::*)(ArgTypes...) const> {};

// Common functor specialization.
template <typename FunctorType, typename ReturnType, typename... ArgTypes>
struct callable_traits<ReturnType (FunctorType::*)(ArgTypes...) const>
    : public callable_traits<ReturnType (*)(ArgTypes...)> {
  using type = FunctorType;
};

// Function pointer specialization.
template <typename ReturnType, typename... ArgTypes>
struct callable_traits<ReturnType (*)(ArgTypes...)>
    : public callable_traits<ReturnType(ArgTypes...)> {
  using type = ReturnType (*)(ArgTypes...);
};

// Base specialization.
template <typename ReturnType, typename... ArgTypes>
struct callable_traits<ReturnType(ArgTypes...)> {
  using signature = ReturnType(ArgTypes...);
  using return_type = ReturnType;
  using args = std::tuple<ArgTypes...>;

  callable_traits() = delete;
};

template <typename FuncA, typename FuncB>
struct SameInterfaceImpl {
  static constexpr bool args_equal = std::is_same<typename callable_traits<FuncA>::args,
                                                  typename callable_traits<FuncB>::args>::value;

  static constexpr bool return_equal =
      std::is_same<typename callable_traits<FuncA>::return_type,
                   typename callable_traits<FuncB>::return_type>::value;

  static constexpr bool value = args_equal && return_equal;
};

template <typename FuncA, typename FuncB>
constexpr bool SameInterface = SameInterfaceImpl<FuncA, FuncB>::value;

template <typename FuncA, typename FuncB>
constexpr bool SameArguments = SameInterfaceImpl<FuncA, FuncB>::args_equal;

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_INTERNAL_CALLABLE_TRAITS_H_
