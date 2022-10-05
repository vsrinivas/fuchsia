// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_TYPE_TRAITS_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_TYPE_TRAITS_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/type_traits.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/tuple.h>

namespace fasync {
namespace internal {

template <size_t I>
struct priority_tag : priority_tag<I - 1> {};

template <>
struct priority_tag<0> {};

template <typename...>
struct first;

template <>
struct first<> {};

template <typename T, typename... Ts>
struct first<T, Ts...> {
  using type = T;
};

template <typename... Ts>
using first_t = typename first<Ts...>::type;

template <typename T>
struct is_tuple : std::false_type {};

template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_tuple_v = is_tuple<T>::value;

template <typename T, typename = void>
struct has_type : std::false_type {};

template <typename T>
struct has_type<T, cpp17::void_t<typename T::type>> : std::true_type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool has_type_v = has_type<T>::value;

template <typename T, typename = void>
struct has_value_type : std::false_type {};

template <typename T>
struct has_value_type<T, cpp17::void_t<typename T::value_type>> : std::true_type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool has_value_type_v = has_type<T>::value;

template <typename R>
struct is_value_result : cpp17::conjunction<is_result<R>, has_value_type<R>>::type {};

template <typename R>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_value_result_v = is_value_result<R>::value;

// Metafunction for whether an |fasync::try_poll| has |.output().value()| or not.
template <typename P>
struct is_value_try_poll : cpp17::conjunction<::fasync::is_try_poll<P>,
                                              is_value_result<::fasync::poll_result_t<P>>>::type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_value_try_poll_v = is_value_try_poll<F>::value;

// Same but for the poll type of a future.
template <typename F>
struct is_value_try_future
    : cpp17::conjunction<is_try_future<F>, is_value_result<::fasync::future_result_t<F>>>::type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_value_try_future_v = is_value_try_future<F>::value;

// A type that no external function will ever use, to check for gerenicity.
struct sentinel {};

// Strategy: go first checking with_context instead of without_context, but exclude generic
// parameters and those that can't take an |fasync::context&| or |const fasync::context&|.
template <typename F, typename = void, typename...>
struct first_param_is_generic : std::false_type {};

template <typename F, typename First, typename... Rest>
struct first_param_is_generic<
    F,
    std::enable_if_t<cpp17::disjunction_v<cpp17::is_invocable<F, sentinel, Rest...>,
                                          cpp17::is_invocable<F, sentinel&, Rest...>>>,
    First, Rest...> : std::true_type {};

template <typename F, typename... Args>
using first_param_is_generic_t = first_param_is_generic<F, void, Args...>;

template <typename F, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool first_param_is_generic_v =
    first_param_is_generic_t<F, Args...>::value;

// The following are used to implement |is_applicable|, the trait that tells us whether we can use
// |std::apply| on a type.
template <typename T, typename V, typename = bool>
struct is_typed_integral_constant : std::false_type {};

template <typename T, typename V>
struct is_typed_integral_constant<T, V, requires_conditions<has_value_type<T>>>
    : cpp17::conjunction<std::is_same<typename T::value_type, V>,
                         std::is_same<decltype(T::value), const V>>::type {};

template <typename T, typename V>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_typed_integral_constant_v =
    is_typed_integral_constant<T, V>::value;

template <typename T, typename = void>
struct has_tuple_size : std::false_type {};

template <typename T>
struct has_tuple_size<T, cpp17::void_t<std::tuple_size<T>>>
    : is_typed_integral_constant<std::tuple_size<T>, size_t>::type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool has_tuple_size_v = has_tuple_size<T>::value;

template <typename T, size_t I, typename = bool>
struct is_gettable_at : std::false_type {};

// TODO(schottm): gate on has_tuple_size first like the concept does
template <typename T, size_t I>
struct is_gettable_at<T, I, requires_conditions<cpp17::bool_constant<(cpp17::tuple_size_v<T> > I)>>>
    : std::is_convertible<decltype(std::get<I>(std::declval<T>())),
                          const std::tuple_element_t<I, T>&>::type {};

template <typename T, size_t I>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_gettable_at_v = is_gettable_at<T, I>::value;

template <typename T, size_t... Is>
LIB_FASYNC_CONSTEVAL bool all_gettable(std::index_sequence<Is...>) {
  return cpp17::conjunction_v<is_gettable_at<T, Is>...>;
}

template <typename T,
          requires_conditions<cpp17::negation<has_tuple_size<cpp20::remove_cvref_t<T>>>> = true>
LIB_FASYNC_CONSTEVAL bool gettable_to_end() {
  return false;
}

template <typename T, typename U = cpp20::remove_cvref_t<T>,
          requires_conditions<has_tuple_size<U>> = true>
LIB_FASYNC_CONSTEVAL bool gettable_to_end() {
  return all_gettable<U>(std::make_index_sequence<cpp17::tuple_size_v<U>>());
}

// This will return true for types for which std::apply (or cpp17::apply) works, e.g. std::tuple and
// std::array.
template <typename T>
struct is_applicable : cpp17::bool_constant<gettable_to_end<T>()> {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_applicable_v = is_applicable<T>::value;

// We can't just put cpp17::apply as a requirement because it would fail the whole build. It doesn't
// SFINAE correctly unless you carefully check your bounds as you iterate.
template <typename H, typename T, size_t... Is>
LIB_FASYNC_CONSTEVAL bool invocable_at_indices(std::index_sequence<Is...>) {
  return cpp17::is_invocable_v<H, std::tuple_element_t<Is, cpp20::remove_cvref_t<T>>&...>;
}

template <typename H, typename T, requires_conditions<cpp17::negation<is_applicable<T>>> = true>
LIB_FASYNC_CONSTEVAL bool applicable_to() {
  return false;
}

template <typename H, typename T, requires_conditions<is_applicable<T>> = true>
LIB_FASYNC_CONSTEVAL bool applicable_to() {
  return invocable_at_indices<H, T>(
      std::make_index_sequence<cpp17::tuple_size_v<cpp20::remove_cvref_t<T>>>());
}

// Trait to detect whether the callable |H| would work in a call to |std::apply| with |T|.
template <typename H, typename T>
struct is_applicable_to : cpp17::bool_constant<applicable_to<H, T>()> {};

template <typename H, typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_applicable_to_v = is_applicable_to<H, T>::value;

struct all_futures final {
  template <typename... Fs, requires_conditions<is_future<Fs>...> = true>
  constexpr void operator()(Fs&&...) {}
};

// Trait to detect whether a given applicable type is composed of futures.a
template <typename T>
struct is_future_applicable
    : cpp17::conjunction<is_applicable<T>, is_applicable_to<all_futures, T>>::type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_future_applicable_v = is_future_applicable<T>::value;

}  // namespace internal
}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_TYPE_TRAITS_H_
