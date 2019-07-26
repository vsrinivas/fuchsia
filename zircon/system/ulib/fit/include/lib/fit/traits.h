// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_TRAITS_H_
#define LIB_FIT_TRAITS_H_

#include <tuple>
#include <type_traits>

namespace fit {

// C++14 compatible polyfill for C++17 traits.
#if defined(__cplusplus) && __cplusplus >= 201703L

template <typename... T>
using void_t = std::void_t<T...>;

template <typename... Ts>
using conjunction = std::conjunction<Ts...>;
template <typename... Ts>
inline constexpr bool conjunction_v = std::conjunction_v<Ts...>;

template <typename... Ts>
using disjunction = std::disjunction<Ts...>;
template <typename... Ts>
inline constexpr bool disjunction_v = std::disjunction_v<Ts...>;

template <typename... Ts>
using negation = std::negation<Ts...>;
template <typename... Ts>
inline constexpr bool negation_v = std::negation_v<Ts...>;

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

// Utility type that negates its truth-like parameter type.
template <typename T>
struct negation : std::integral_constant<bool, !bool(T::value)> {};

template <typename T>
constexpr bool negation_v = negation<T>::value;

#endif

// Encapsulates capture of a parameter pack. Typical use is to use instances of this empty struct
// for type dispatch in function template deduction/overload resolution.
//
// Example:
//  template <typename Callable, typename... Args>
//  auto inspect_args(Callable c, parameter_pack<Args...>) {
//      // do something with Args...
//  }
//
//  template <typename Callable>
//  auto inspect_args(Callable c) {
//      return inspect_args(std::move(c), typename callable_traits<Callable>::args{});
//  }
template <typename... T>
struct parameter_pack {
  static constexpr size_t size = sizeof...(T);

  template <size_t i>
  using at = typename std::tuple_element_t<i, std::tuple<T...>>;
};

// |callable_traits| captures elements of interest from function-like types (functions, function
// pointers, and functors, including lambdas). Due to common usage patterns, const and non-const
// functors are treated identically.
//
// Member types:
//  |args|        - a |parameter_pack| that captures the parameter types of the function. See
//                  |parameter_pack| for usage and details.
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
  using args = parameter_pack<ArgTypes...>;

  callable_traits() = delete;
};

// Determines whether a type has an operator() that can be invoked.
template <typename T, typename = void_t<>>
struct is_callable : public std::false_type {};
template <typename ReturnType, typename... ArgTypes>
struct is_callable<ReturnType (*)(ArgTypes...)> : public std::true_type {};
template <typename FunctorType, typename ReturnType, typename... ArgTypes>
struct is_callable<ReturnType (FunctorType::*)(ArgTypes...)> : public std::true_type {};
template <typename T>
struct is_callable<T, void_t<decltype(&T::operator())>> : public std::true_type {};

}  // namespace fit

#endif  // LIB_FIT_TRAITS_H_
