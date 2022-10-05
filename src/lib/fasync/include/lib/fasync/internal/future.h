// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_FUTURE_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_FUTURE_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

// fit::function is still C++14-compliant, but when we compile in C++17 mode these warnings still
// fire.
LIB_FASYNC_IGNORE_CPP14_COMPAT_BEGIN
#include <lib/fit/function.h>
LIB_FASYNC_IGNORE_CPP14_COMPAT_END

#include <lib/fasync/internal/type_traits.h>
#include <lib/fasync/poll.h>
#include <lib/fasync/type_traits.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/functional.h>
#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/tuple.h>
#include <lib/stdcompat/type_traits.h>
#include <lib/stdcompat/variant.h>

#include <algorithm>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fasync {

class context;

namespace internal {

using ::fit::internal::requires_conditions;

using ::fit::internal::is_match;
using ::fit::internal::is_match_v;

using ::fit::internal::is_error;
using ::fit::internal::is_error_v;

using ::fit::internal::is_success;
using ::fit::internal::is_success_v;

using ::fit::internal::is_result;
using ::fit::internal::is_result_v;

template <typename T, requires_conditions<cpp17::negation<std::is_move_assignable<T>>> = true>
constexpr void move_construct_optional(cpp17::optional<T>& to, T&& from) {
  to.emplace(std::move(from));
}

template <typename T, requires_conditions<std::is_move_assignable<T>> = true>
constexpr void move_construct_optional(cpp17::optional<T>& to, T&& from) {
  to = std::move(from);
}

////// BEGIN CONCEPTS EQUIVALENTS /////////

template <typename H, typename... Args>
struct is_invocable_with_applicable : std::false_type {};

template <typename H, typename T>
struct is_invocable_with_applicable<H, T> : is_applicable_to<H, T>::type {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_with_applicable_v =
    is_invocable_with_applicable<H, Args...>::value;

template <typename T>
struct is_context_arg : cpp17::conjunction<std::is_lvalue_reference<T>,
                                           std::is_base_of<context, cpp20::remove_cvref_t<T>>> {};

template <typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_context_arg_v = is_context_arg<Args...>::value;

template <typename... Args>
struct has_context_arg : is_context_arg<first_t<Args...>>::type {};

template <>
struct has_context_arg<> : std::false_type {};

template <typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool has_context_arg_v = has_context_arg<Args...>::value;

template <typename H, typename... Args>
constexpr auto args_as_list_impl(std::nullptr_t)
    -> decltype(std::declval<H>()({std::declval<Args>()...}), std::true_type());

template <typename H, typename... Args>
constexpr std::false_type args_as_list_impl(...);

template <typename H, typename... Args>
struct is_invocable_with_args_as_list : decltype(args_as_list_impl<H, Args...>(nullptr)) {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_with_args_as_list_v =
    is_invocable_with_args_as_list<H, Args...>::value;

////// END CONCEPTS EQUIVALENTS /////////

}  // namespace internal

namespace internal {

template <typename...>
struct delay_compile_error : std::false_type {};

template <typename H, typename... Args>
constexpr void invoke_compile_error(H&& handler, Args&&... args) {
  static_assert(delay_compile_error<H, Args...>::value,
                "Your handler does not meet the requirements in order to be valid.\n"
                "See the documentation of |fasync::future| for more information\n");
}

template <typename H, typename T, typename = bool>
struct apply_result {};

template <typename H, typename T>
struct apply_result<H, T, requires_conditions<is_applicable_to<H, T>>> {
  using type = decltype(cpp17::apply(std::declval<H>(), std::declval<T>()));
};

template <typename H, typename T>
using apply_result_t = typename apply_result<H, T>::type;

template <typename StoredF, typename... BoundArgs>
class ref_binder {
 public:
  template <typename F, typename... Args>
  explicit constexpr ref_binder(F&& f, Args&&... args)
      : stored_f_(std::forward<F>(f)), bound_args_(std::forward<Args>(args)...) {}

  // TODO(schottm): do other ref-qualifier overloads, might have to go back to the old
  // std::index_sequence way to make it work
  template <typename... CallArgs,
            requires_conditions<cpp17::is_invocable<StoredF, BoundArgs..., CallArgs...>> = true>
  constexpr decltype(auto) operator()(CallArgs&&... call_args) {
    return cpp17::apply(
        [this](auto&&... all_args) -> decltype(auto) {
          return cpp20::invoke(stored_f_, std::forward<decltype(all_args)>(all_args)...);
        },
        std::tuple_cat(std::move(bound_args_),
                       std::forward_as_tuple(std::forward<CallArgs>(call_args)...)));
  }

 private:
  StoredF stored_f_;
  LIB_FASYNC_NO_UNIQUE_ADDRESS std::tuple<BoundArgs...> bound_args_;
};

// cpp20::bind_front is not sufficient since it can't store references or unwrap
// std::reference_wrapper
template <typename F, typename... BoundArgs>
auto bind_refs(F&& f, BoundArgs&&... bound_args) {
  // Note: the way forwarding works, this binder is designed to store lvalue references directly.
  return ref_binder<F, BoundArgs...>(std::forward<F>(f), std::forward<BoundArgs>(bound_args)...);
}

template <typename H, typename... Args,
          requires_conditions<cpp17::bool_constant<(sizeof...(Args) == 1)>,
                              is_applicable<first_t<Args...>>> = true>
constexpr decltype(auto) invoke_with_applicable_as_list(H&& handler, Args&&... args) {
  return cpp17::apply(
      [&](auto&&... list) -> decltype(auto) {
        return std::forward<H>(handler)({std::forward<decltype(list)>(list)...});
      },
      std::forward<Args>(args)...);
}

}  // namespace internal

namespace internal {

////// BEGIN CONCEPTS EQUIVALENTS /////////

template <typename H, typename... Args>
constexpr auto applicable_as_list_impl(std::nullptr_t)
    -> decltype(std::declval<H>()({}),
                invoke_with_applicable_as_list(std::declval<H>(), std::declval<Args>()...),
                std::true_type());

template <typename H, typename... Args>
constexpr std::false_type applicable_as_list_impl(...);

template <typename H, typename... Args>
struct is_invocable_with_applicable_as_list
    : decltype(applicable_as_list_impl<H, Args...>(nullptr)) {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_with_applicable_as_list_v =
    is_invocable_with_applicable_as_list<H, Args...>::value;

template <typename H, typename... Args>
struct is_invocable_handler_internal
    : cpp17::conjunction<
          cpp17::negation<has_context_arg<Args...>>,
          cpp17::disjunction<
              is_invocable_with_applicable<H, Args...>, cpp17::is_invocable<H, Args...>,
              is_invocable_with_args_as_list<H, Args...>,
              is_invocable_with_applicable_as_list<H, Args...>, cpp17::is_invocable<H>>>::type {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_handler_internal_v =
    is_invocable_handler_internal<H, Args...>::value;

////// END CONCEPTS EQUIVALENTS /////////

template <typename H, typename T, requires_conditions<is_invocable_with_applicable<H, T>> = true>
constexpr apply_result_t<H, T> invoke_handler_internal(priority_tag<4>, H&& handler,
                                                       T&& applicable) {
  return cpp17::apply(std::forward<H>(handler), std::forward<T>(applicable));
}

template <typename H, typename... Args,
          requires_conditions<cpp17::negation<is_invocable_with_applicable<H, Args...>>,
                              cpp17::is_invocable<H, Args...>> = true>
constexpr cpp17::invoke_result_t<H, Args...> invoke_handler_internal(priority_tag<3>, H&& handler,
                                                                     Args&&... args) {
  return cpp20::invoke(std::forward<H>(handler), std::forward<Args>(args)...);
}

template <typename H, typename... Args,
          requires_conditions<is_invocable_with_args_as_list<H, Args...>> = true>
constexpr auto invoke_handler_internal(priority_tag<2>, H&& handler, Args&&... args)
    -> decltype(std::forward<H>(handler)({std::forward<Args>(args)...})) {
  return std::forward<H>(handler)({std::forward<Args>(args)...});
}

template <typename H, typename T,
          requires_conditions<is_invocable_with_applicable_as_list<H, T>> = true>
constexpr auto invoke_handler_internal(priority_tag<1>, H&& handler, T&& applicable)
    -> decltype(invoke_with_applicable_as_list(std::forward<H>(handler),
                                               std::forward<T>(applicable))) {
  return invoke_with_applicable_as_list(std::forward<H>(handler), std::forward<T>(applicable));
}

template <typename H, typename... Args>
constexpr decltype(auto) invoke_handler_internal(priority_tag<0>, H&& handler, Args&&... args) {
  // H wants to ignore all args
  return cpp20::invoke(std::forward<H>(handler));
}

template <typename H, typename... Args,
          requires_conditions<is_invocable_handler_internal<H, Args...>> = true>
constexpr decltype(auto) invoke_handler_internal(H&& handler, Args&&... args) {
  return invoke_handler_internal(priority_tag<4>(), std::forward<H>(handler),
                                 std::forward<Args>(args)...);
}

template <typename H, typename... Args,
          requires_conditions<is_invocable_handler_internal<H, Args...>> = true>
constexpr decltype(auto) invoke_without_context(H&& handler, ::fasync::context&, Args&&... args) {
  return invoke_handler_internal(std::forward<H>(handler), std::forward<Args>(args)...);
}

template <typename H, typename... Args,
          requires_conditions<
              cpp17::negation<first_param_is_generic_t<H, Args...>>,
              is_invocable_handler_internal<decltype(bind_refs(std::declval<H>(),
                                                               std::declval<::fasync::context&>())),
                                            Args...>> = true>
constexpr decltype(auto) invoke_with_context(H&& handler, ::fasync::context& context,
                                             Args&&... args) {
  return invoke_handler_internal(bind_refs(std::forward<H>(handler), context),
                                 std::forward<Args>(args)...);
}

template <typename H, typename... Args,
          requires_conditions<
              cpp17::negation<first_param_is_generic_t<H, Args...>>,
              is_invocable_handler_internal<decltype(bind_refs(std::declval<H>(),
                                                               std::declval<::fasync::context&>())),
                                            Args...>> = true>
constexpr decltype(auto) invoke_with_context(H&& handler, const ::fasync::context& context,
                                             Args&&... args) {
  return invoke_handler_internal(bind_refs(std::forward<H>(handler), context),
                                 std::forward<Args>(args)...);
}

}  // namespace internal

namespace internal {

////// BEGIN CONCEPTS EQUIVALENTS /////////

template <typename H, typename... Args>
constexpr auto without_context_impl(std::nullptr_t)
    -> decltype(invoke_without_context(std::declval<H>(), std::declval<Args>()...),
                std::true_type());

template <typename H, typename... Args>
constexpr std::false_type without_context_impl(...);

template <typename H, typename... Args>
struct is_invocable_handler_internal_without_context
    : decltype(without_context_impl<H, Args...>(nullptr)) {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_handler_internal_without_context_v =
    is_invocable_handler_internal_without_context<H, Args...>::value;

template <typename H, typename... Args>
constexpr auto with_context_impl(std::nullptr_t)
    -> decltype(invoke_with_context(std::declval<H>(), std::declval<Args>()...), std::true_type());

template <typename H, typename... Args>
constexpr std::false_type with_context_impl(...);

template <typename H, typename... Args>
struct is_invocable_handler_internal_with_context
    : decltype(with_context_impl<H, Args...>(nullptr)) {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_handler_internal_with_context_v =
    is_invocable_handler_internal_with_context<H, Args...>::value;

// TODO(schottm): it might not be useful to define it like this with the context shoved in; what if
// your Args already has a context?
template <typename H, typename... Args>
struct is_invocable_handler
    : cpp17::disjunction<
          is_invocable_handler_internal_without_context<H, ::fasync::context&, Args...>,
          is_invocable_handler_internal_with_context<H, ::fasync::context&, Args...>>::type {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_invocable_handler_v =
    is_invocable_handler<H, Args...>::value;

////// END CONCEPTS EQUIVALENTS /////////

template <typename H, typename... Args,
          requires_conditions<is_invocable_handler_internal_without_context<H, Args...>> = true>
constexpr decltype(auto) invoke_handler_tag(priority_tag<2>, H&& handler, Args&&... args) {
  return invoke_without_context(std::forward<H>(handler), std::forward<Args>(args)...);
}

template <
    typename H, typename... Args,
    requires_conditions<cpp17::negation<is_invocable_handler_internal_without_context<H, Args...>>,
                        is_invocable_handler_internal_with_context<H, Args...>> = true>
constexpr decltype(auto) invoke_handler_tag(priority_tag<1>, H&& handler, Args&&... args) {
  return invoke_with_context(std::forward<H>(handler), std::forward<Args>(args)...);
}

template <typename H, typename... Args>
constexpr decltype(auto) invoke_handler_tag(priority_tag<0>, H&& handler, Args&&... args) {
  return invoke_compile_error(std::forward<H>(handler), std::forward<Args>(args)...);
}

template <typename H, typename... Args>
constexpr decltype(auto) invoke_handler(H&& handler, Args&&... args) {
  return invoke_handler_tag(priority_tag<2>(), std::forward<H>(handler),
                            std::forward<Args>(args)...);
}

template <typename H, typename... Args,
          requires_conditions<is_invocable_handler<H, Args...>> = true>
constexpr auto invoke_handler_type_impl()
    -> decltype(invoke_handler(std::declval<H>(), std::declval<::fasync::context&>(),
                               std::declval<Args>()...));

template <typename Enable, typename H, typename... Args>
struct invoke_handler_type {};

template <typename H, typename... Args>
struct invoke_handler_type<cpp17::void_t<decltype(invoke_handler_type_impl<H, Args...>())>, H,
                           Args...>
    : cpp20::type_identity<decltype(invoke_handler_type_impl<H, Args...>())> {};

template <typename H, typename... Args>
using invoke_handler_t = typename invoke_handler_type<void, H, Args...>::type;

template <typename Enable, typename H, typename... Args>
struct handler_returns_void_for_impl : std::false_type {};

template <typename H, typename... Args>
struct handler_returns_void_for_impl<cpp17::void_t<invoke_handler_t<H, Args...>>, H, Args...>
    : std::is_void<invoke_handler_t<H, Args...>>::type {};

// Metafunction for determining whether a callable returns void given the provided argument types.
template <typename H, typename... Args>
struct handler_returns_void_for : handler_returns_void_for_impl<void, H, Args...>::type {};

template <typename H, typename... Args>
LIB_FASYNC_INLINE_CONSTANT constexpr bool handler_returns_void_for_v =
    handler_returns_void_for<H, Args...>::value;

template <typename F>
class poller;

template <typename T>
struct promote_return_type {
  using type = T;
};

template <typename... Ts>
struct promote_return_type<::fit::success<Ts...>> {
  using type = ::fit::result<fit::failed, Ts...>;
};

template <typename E>
struct promote_return_type<::fit::error<E>> {
  using type = ::fit::result<E>;
};

template <>
struct promote_return_type<::fit::failed> {
  using type = ::fit::result<::fit::failed>;
};

template <typename... Ts>
struct promote_return_type<::fasync::ready<::fit::success<Ts...>>> {
  using type = ::fasync::try_poll<fit::failed, Ts...>;
};

template <typename E>
struct promote_return_type<::fasync::ready<::fit::error<E>>> {
  using type = ::fasync::try_poll<E>;
};

template <>
struct promote_return_type<::fasync::ready<::fit::failed>> {
  using type = ::fasync::try_poll<::fit::failed>;
};

template <typename T>
using promote_return_type_t = typename promote_return_type<cpp20::remove_cvref_t<T>>::type;

template <typename R, typename T>
struct merge_ok_result {};

// TODO(schottm): necessary for returning futures?
template <typename E, typename... Ts, typename U>
struct merge_ok_result<::fit::result<E, Ts...>, U> {
  using type = ::fit::result<E, U>;
};

template <typename E, typename... Ts>
struct merge_ok_result<::fit::result<E, Ts...>, ::fit::failed> {
  using type = ::fit::result<::fit::failed, Ts...>;
};

template <typename E, typename... Ts, typename F>
struct merge_ok_result<::fit::result<E, Ts...>, ::fit::error<F>> {
  using type = ::fit::result<F, Ts...>;
};

template <typename E, typename... Ts, typename... Us>
struct merge_ok_result<::fit::result<E, Ts...>, ::fit::success<Us...>> {
  using type = ::fit::result<E, Us...>;
};

// TODO(schottm): is this really what we want?
// Probably not; make one merge_result and ditch the ok/error distinction?
template <typename E, typename... Ts, typename F, typename... Us>
struct merge_ok_result<::fit::result<E, Ts...>, ::fit::result<F, Us...>> {
  using type = ::fit::result<E, Us...>;
};

template <typename E, typename... Ts>
struct merge_ok_result<::fit::result<E, Ts...>, void> {
  using type = ::fit::result<E>;
};

template <typename R, typename T>
using merge_ok_result_t =
    typename merge_ok_result<cpp20::remove_cvref_t<R>, cpp20::remove_cvref_t<T>>::type;

template <typename R, typename E>
struct merge_error_result {};

template <typename E, typename... Ts, typename F>
struct merge_error_result<::fit::result<E, Ts...>, F> {
  using type = ::fit::result<F, Ts...>;
};

template <typename E, typename... Ts>
struct merge_error_result<::fit::result<E, Ts...>, ::fit::failed> {
  using type = ::fit::result<::fit::failed, Ts...>;
};

template <typename E, typename... Ts, typename F>
struct merge_error_result<::fit::result<E, Ts...>, ::fit::error<F>> {
  using type = ::fit::result<F, Ts...>;
};

template <typename E, typename... Ts, typename... Us>
struct merge_error_result<::fit::result<E, Ts...>, ::fit::success<Us...>> {
  using type = ::fit::result<E, Us...>;
};

template <typename E, typename... Ts, typename F, typename... Us>
struct merge_error_result<::fit::result<E, Ts...>, ::fit::result<F, Us...>> {
  using type = ::fit::result<F, Ts...>;
};

template <typename E, typename... Ts>
struct merge_error_result<::fit::result<E, Ts...>, void> {
  using type = ::fit::result<E>;
};

template <typename R, typename E>
using merge_error_result_t =
    typename merge_error_result<cpp20::remove_cvref_t<R>, cpp20::remove_cvref_t<E>>::type;

// Arbitrary types are neither success nor error.
template <typename R, typename T>
struct merge_result {
  using type = T;
};

template <typename R>
struct merge_result<R, ::fit::failed> {
  using type = merge_error_result_t<R, ::fit::failed>;
};

template <typename R, typename E>
struct merge_result<R, ::fit::error<E>> {
  using type = merge_error_result_t<R, ::fit::error<E>>;
};

template <typename R, typename... Ts>
struct merge_result<R, ::fit::success<Ts...>> {
  using type = merge_ok_result_t<R, ::fit::success<Ts...>>;
};

template <typename R, typename T>
using merge_result_t = typename merge_result<R, T>::type;

////////// FORWARD_TO_*

// These |forward_to_*_result()| functions take incomplete result types like |fit::success| and
// |fit::error| and return a result appropriately derived from the given result type.
template <typename R, typename T, requires_conditions<is_future<T>> = true>
constexpr merge_ok_result_t<R, T> forward_to_ok_result(T&& value) {
  return merge_ok_result_t<R, T>(::fit::ok(std::forward<T>(value)));
}

template <typename R>
constexpr merge_ok_result_t<R, ::fit::failed> forward_to_ok_result(::fit::failed) {
  return merge_ok_result_t<R, ::fit::failed>(::fit::failed());
}

template <typename R, typename E>
constexpr merge_ok_result_t<R, ::fit::error<E>> forward_to_ok_result(::fit::error<E>&& error) {
  return merge_ok_result_t<R, ::fit::error<E>>(std::forward<::fit::error<E>>(error));
}

template <typename R, typename... Ts>
constexpr merge_ok_result_t<R, ::fit::success<Ts...>> forward_to_ok_result(
    ::fit::success<Ts...>&& success) {
  return merge_ok_result_t<R, ::fit::success<Ts...>>(std::forward<::fit::success<Ts...>>(success));
}

template <typename R, typename E, typename... Ts>
constexpr merge_ok_result_t<R, ::fit::result<E, Ts...>> forward_to_ok_result(
    ::fit::result<E, Ts...>&& result) {
  return merge_ok_result_t<R, ::fit::result<E, Ts...>>(
      std::forward<::fit::result<E, Ts...>>(result));
}

template <typename R, typename T, requires_conditions<is_future<T>> = true>
constexpr merge_error_result_t<R, T> forward_to_error_result(T&& value) {
  return merge_error_result_t<R, T>(::fit::error<T>(std::forward<T>(value)));
}

template <typename R>
constexpr merge_error_result_t<R, ::fit::failed> forward_to_error_result(::fit::failed) {
  return merge_error_result_t<R, ::fit::failed>(::fit::failed());
}

template <typename R, typename E>
constexpr merge_error_result_t<R, ::fit::error<E>> forward_to_error_result(
    ::fit::error<E>&& error) {
  return merge_error_result_t<R, ::fit::error<E>>(std::forward<::fit::error<E>>(error));
}

template <typename R, typename... Ts>
constexpr merge_error_result_t<R, ::fit::success<Ts...>> forward_to_error_result(
    ::fit::success<Ts...>&& success) {
  return merge_error_result_t<R, ::fit::success<Ts...>>(
      std::forward<::fit::success<Ts...>>(success));
}

template <typename R, typename E, typename... Ts>
constexpr decltype(auto) forward_to_error_result(::fit::result<E, Ts...>&& result) {
  return std::forward<::fit::result<E, Ts...>>(result);
}

template <typename R, typename T>
constexpr merge_result_t<R, T> forward_to_result(T&& value) {
  return merge_result_t<R, T>(std::forward<T>(value));
}

////////// FORWARD_TO_*

/////////// HANDLE_RESULT //////////////

template <typename H, typename R, requires_conditions<is_result<R>> = true>
using handle_result_t = merge_result_t<R, invoke_handler_t<H, R>>;

template <typename H, typename R,
          requires_conditions<cpp17::negation<handler_returns_void_for<H, R>>> = true>
constexpr handle_result_t<H, R> handle_result(H&& handler, ::fasync::context& context, R&& result) {
  return forward_to_result<R>(
      invoke_handler(std::forward<H>(handler), context, std::forward<R>(result)));
}

template <typename H, typename R, requires_conditions<handler_returns_void_for<H, R>> = true>
constexpr handle_result_t<H, R> handle_result(H&& handler, ::fasync::context& context, R&& result) {
  invoke_handler(std::forward<H>(handler), context, std::forward<R>(result));
  return forward_to_result<R>(::fit::ok());
}

/////////// HANDLE_RESULT //////////////

////////// HANDLE_OUTPUT //////////////

template <typename H, typename... Ts>
struct handle_output_type {
  using type = promote_return_type_t<invoke_handler_t<H, Ts...>>;
};

// TODO(schottm): is_result and matching on result have different semantics
template <typename H, typename E, typename... Ts>
struct handle_output_type<H, ::fit::result<E, Ts...>> {
  using type = handle_result_t<H, ::fit::result<E, Ts...>>;
};

template <typename H, typename... Ts>
using handle_output_t = typename handle_output_type<H, Ts...>::type;

template <typename H>
constexpr handle_output_t<H> handle_output(H&& handler, ::fasync::context& context) {
  return invoke_handler(std::forward<H>(handler), context);
}

template <typename H, typename T, requires_conditions<cpp17::negation<is_result<T>>> = true>
constexpr handle_output_t<H, T> handle_output(H&& handler, ::fasync::context& context, T&& value) {
  return invoke_handler(std::forward<H>(handler), context, std::forward<T>(value));
}

template <typename H, typename R, requires_conditions<is_result<R>> = true>
constexpr handle_output_t<H, R> handle_output(H&& handler, ::fasync::context& context, R&& result) {
  return handle_result(std::forward<H>(handler), context, std::forward<R>(result));
}

////////// HANDLE_OUTPUT //////////////

////////// HANDLE_VALUE //////////////

template <typename H, typename R, requires_conditions<is_result<R>> = true>
using handle_ok_result_t = merge_ok_result_t<R, invoke_handler_t<H, R>>;

template <typename H, typename R,
          requires_conditions<is_result<R>, cpp17::negation<handler_returns_void_for<H, R>>> = true>
constexpr handle_ok_result_t<H, R> handle_ok_result(H&& handler, ::fasync::context& context,
                                                    R&& result) {
  return forward_to_ok_result<R>(
      invoke_handler(std::forward<H>(handler), context, std::forward<R>(result)));
}

template <typename H, typename R,
          requires_conditions<is_result<R>, handler_returns_void_for<H, R>> = true>
constexpr handle_ok_result_t<H, R> handle_ok_result(H&& handler, ::fasync::context& context,
                                                    R&& result) {
  invoke_handler(std::forward<H>(handler), context, std::forward<R>(result));
  return forward_to_ok_result<R>(::fit::ok());
}

template <typename H, typename... Ts>
struct handle_value_type {
  using type = invoke_handler_t<H, Ts...>;
};

template <typename H, typename E, typename... Ts>
struct handle_value_type<H, ::fit::result<E, Ts...>> {
  using type = handle_ok_result_t<H, ::fit::result<E, Ts...>>;
};

template <typename H, typename... Ts>
using handle_value_t = typename handle_value_type<H, Ts...>::type;

template <typename H>
constexpr invoke_handler_t<H> handle_value(H&& handler, ::fasync::context& context) {
  return invoke_handler(std::forward<H>(handler), context);
}

template <typename H, typename T, requires_conditions<cpp17::negation<is_result<T>>> = true>
constexpr handle_value_t<H, T> handle_value(H&& handler, ::fasync::context& context, T&& value) {
  return invoke_handler(std::forward<H>(handler), context, std::forward<T>(value));
}

template <typename H, typename R, requires_conditions<is_result<R>> = true>
constexpr handle_value_t<H, R> handle_value(H&& handler, ::fasync::context& context, R&& result) {
  return handle_ok_result(std::forward<H>(handler), context, std::forward<R>(result));
}

////////// HANDLE_VALUE //////////////

////////// HANDLE_ERROR //////////////

template <typename H, typename R, requires_conditions<is_result<R>> = true>
using handle_error_result_t = merge_error_result_t<R, invoke_handler_t<H, R>>;

template <typename H, typename... Ts>
struct handle_error_type {
  using type = invoke_handler_t<H, Ts...>;
};

template <typename H, typename E, typename... Ts>
struct handle_error_type<H, ::fit::result<E, Ts...>> {
  using type = handle_error_result_t<H, ::fit::result<E, Ts...>>;
};

template <typename H, typename... Ts>
using handle_error_t = typename handle_error_type<H, Ts...>::type;

template <typename H, typename R,
          requires_conditions<cpp17::negation<handler_returns_void_for<H, R>>> = true>
constexpr handle_error_result_t<H, R> handle_error_result(H&& handler, ::fasync::context& context,
                                                          R&& result) {
  return forward_to_error_result<R>(
      invoke_handler(std::forward<H>(handler), context, std::forward<R>(result)));
}

// TODO(schottm): should this be ok instead? or not allowed? or just forward the previous result?
template <typename H, typename R, requires_conditions<handler_returns_void_for<H, R>> = true>
constexpr handle_error_result_t<H, R> handle_error_result(H&& handler, ::fasync::context& context,
                                                          R&& result) {
  invoke_handler(std::forward<H>(handler), context, std::forward<R>(result));
  return forward_to_error_result<R>(::fit::ok());
}

template <typename H, typename T, requires_conditions<cpp17::negation<is_result<T>>> = true>
constexpr handle_error_t<H, T> handle_error(H&& handler, ::fasync::context& context, T&& value) {
  return invoke_handler(std::forward<H>(handler), context, std::forward<T>(value));
}

template <typename H, typename R, requires_conditions<is_result<R>> = true>
constexpr handle_error_t<H, R> handle_error(H&& handler, ::fasync::context& context, R&& result) {
  return handle_error_result(std::forward<H>(handler), context, std::forward<R>(result));
}

////////// HANDLE_ERROR //////////////

// Note: this bypasses the reason we have a separate fasync::ready in the first place and should
// never be used outside these specific internal functions.
inline constexpr ::fasync::poll<> to_poll(::fasync::pending pending) { return pending; }

template <typename... Ts>
constexpr ::fasync::poll<Ts...> to_poll(::fasync::ready<Ts...>&& ready) {
  return std::forward<::fasync::ready<Ts...>>(ready);
}

template <typename... Ts>
constexpr ::fasync::poll<Ts...> to_poll(::fasync::poll<Ts...>&& poll) {
  return std::forward<::fasync::poll<Ts...>>(poll);
}

// TODO: is this necessary? if not, is this entire overload set necessary?
template <typename T>
constexpr ::fasync::poll<T> to_poll(T&& value) {
  return ::fasync::done(std::forward<T>(value));
}

//////////// HANDLE_MAP ///////////////

template <typename H, typename F,
          requires_conditions<is_void_future<F>, handler_returns_void_for<H>> = true>
constexpr ::fasync::poll<> handle_map(H&& handler, ::fasync::context& context, poller<F>& poller) {
  handle_output(std::forward<H>(handler), context);
  return ::fasync::done();
}

template <
    typename H, typename F,
    requires_conditions<is_void_future<F>, cpp17::negation<handler_returns_void_for<H>>> = true>
constexpr auto handle_map(H&& handler, ::fasync::context& context, poller<F>& poller) {
  return to_poll(handle_output(std::forward<H>(handler), context));
}

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_void_future<F>>,
                              handler_returns_void_for<H, future_output_t<F>&>> = true>
constexpr ::fasync::poll<> handle_map(H&& handler, ::fasync::context& context, poller<F>& poller) {
  invoke_handler(std::forward<H>(handler), context, poller.output());
  return ::fasync::done();
}

template <
    typename H, typename F,
    requires_conditions<cpp17::negation<is_void_future<F>>,
                        cpp17::negation<handler_returns_void_for<H, future_output_t<F>&>>> = true>
constexpr auto handle_map(H&& handler, ::fasync::context& context, poller<F>& poller) {
  return to_poll(handle_output(std::forward<H>(handler), context, poller.output()));
}

template <typename H, typename F>
using handle_map_t = decltype(handle_map(std::declval<H>(), std::declval<::fasync::context&>(),
                                         std::declval<poller<F>&>()));

//////////// HANDLE_MAP ///////////////

//////////// HANDLE_MAP_OK ///////////////

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_value_try_future<F>>,
                              handler_returns_void_for<H>> = true>
constexpr ::fasync::future_poll_t<F> handle_map_ok(priority_tag<9>, H&& handler,
                                                   ::fasync::context& context, poller<F>& poller) {
  invoke_handler(std::forward<H>(handler), context);
  return ::fasync::done(::fit::ok());
}

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_value_try_future<F>>,
                              cpp17::negation<handler_returns_void_for<H>>> = true>
constexpr auto handle_map_ok(priority_tag<8>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return to_poll(::fasync::done(forward_to_ok_result<::fasync::future_result_t<F>>(
      handle_value(std::forward<H>(handler), context))));
}

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_value_try_future<F>>,
                              is_poll<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<7>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return handle_value(std::forward<H>(handler), context);
}

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_value_try_future<F>>,
                              is_ready<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<6>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return to_poll(handle_value(std::forward<H>(handler), context));
}

template <typename H, typename F,
          requires_conditions<cpp17::negation<is_value_try_future<F>>,
                              is_pending<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<5>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return ::fasync::future_poll_t<F>(invoke_handler(std::forward<H>(handler), context));
}

template <typename H, typename F,
          requires_conditions<is_value_try_future<F>,
                              handler_returns_void_for<H, ::fasync::future_value_t<F>&>> = true>
constexpr auto handle_map_ok(priority_tag<4>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  invoke_handler(std::forward<H>(handler), context, poller.output().value());
  return ::fasync::try_poll<::fasync::future_error_t<F>>(::fasync::done(::fit::ok()));
}

template <typename H, typename F,
          requires_conditions<is_value_try_future<F>,
                              is_poll<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<3>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return handle_value(std::forward<H>(handler), context, poller.output().value());
}

template <typename H, typename F,
          requires_conditions<is_value_try_future<F>,
                              is_ready<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<2>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return to_poll(handle_value(std::forward<H>(handler), context, poller.output().value()));
}

template <typename H, typename F,
          requires_conditions<is_value_try_future<F>,
                              is_pending<invoke_handler_t<H, ::fasync::future_value_t<F>&>>> = true>
constexpr auto handle_map_ok(priority_tag<1>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return ::fasync::future_poll_t<F>(
      invoke_handler(std::forward<H>(handler), context, poller.output().value()));
}

template <typename H, typename F, typename R = invoke_handler_t<H, ::fasync::future_value_t<F>&>,
          requires_conditions<is_value_try_future<F>, cpp17::negation<std::is_void<R>>,
                              cpp17::negation<is_poll<R>>> = true>
constexpr auto handle_map_ok(priority_tag<0>, H&& handler, ::fasync::context& context,
                             poller<F>& poller) {
  return to_poll(::fasync::done(forward_to_ok_result<::fasync::future_result_t<F>>(
      handle_value(std::forward<H>(handler), context, poller.output().value()))));
}

template <typename H, typename F>
constexpr auto handle_map_ok(H&& handler, ::fasync::context& context, poller<F>& poller) {
  return handle_map_ok(priority_tag<9>(), std::forward<H>(handler), context, poller);
}

template <typename H, typename F>
using handle_map_ok_t = decltype(handle_map_ok(
    std::declval<H>(), std::declval<::fasync::context&>(), std::declval<poller<F>&>()));

//////////// HANDLE_MAP_OK ///////////////

//////////// HANDLE_MAP_ERROR ///////////////

template <typename H, typename F,
          requires_conditions<handler_returns_void_for<H, ::fasync::future_error_t<F>&>> = true>
constexpr auto handle_map_error(priority_tag<4>, H&& handler, ::fasync::context& context,
                                poller<F>& poller) {
  static_assert(!is_value_try_future_v<F>,
                "Returning void from an error handler is only supported when the previous result "
                "had no |value_type|.");
  invoke_handler(std::forward<H>(handler), context, poller.output().error_value());
  return ::fasync::future_poll_t<F>(::fasync::done(::fit::ok()));
}

template <typename H, typename F,
          requires_conditions<is_poll<invoke_handler_t<H, ::fasync::future_error_t<F>&>>> = true>
constexpr auto handle_map_error(priority_tag<3>, H&& handler, ::fasync::context& context,
                                poller<F>& poller) {
  return handle_error(std::forward<H>(handler), context, poller.output().error_value());
}

template <typename H, typename F,
          requires_conditions<is_ready<invoke_handler_t<H, ::fasync::future_error_t<F>&>>> = true>
constexpr auto handle_map_error(priority_tag<2>, H&& handler, ::fasync::context& context,
                                poller<F>& poller) {
  return to_poll(handle_error(std::forward<H>(handler), context, poller.output().error_value()));
}

template <typename H, typename F,
          requires_conditions<is_pending<invoke_handler_t<H, ::fasync::future_error_t<F>&>>> = true>
constexpr auto handle_map_error(priority_tag<1>, H&& handler, ::fasync::context& context,
                                poller<F>& poller) {
  return ::fasync::future_poll_t<F>(
      invoke_handler(std::forward<H>(handler), context, poller.output().error_value()));
}

template <typename H, typename F>
constexpr auto handle_map_error(priority_tag<0>, H&& handler, ::fasync::context& context,
                                poller<F>& poller) {
  return to_poll(::fasync::done(forward_to_error_result<::fasync::future_result_t<F>>(
      handle_error(std::forward<H>(handler), context, poller.output().error_value()))));
}

template <typename H, typename F>
constexpr auto handle_map_error(H&& handler, ::fasync::context& context, poller<F>& poller) {
  return handle_map_error(priority_tag<4>(), std::forward<H>(handler), context, poller);
}

template <typename H, typename F>
using handle_map_error_t = decltype(handle_map_error(
    std::declval<H>(), std::declval<::fasync::context&>(), std::declval<poller<F>&>()));

//////////// HANDLE_MAP_ERROR ///////////////

template <typename H, typename F, typename = bool>
struct handler_output {
  using type = decltype(::fasync::internal::invoke_handler(
      std::declval<H>(), std::declval<::fasync::context&>(),
      std::declval<::fasync::future_output_t<F>&>()));
};

template <typename H, typename F>
struct handler_output<H, F, requires_conditions<is_void_future<F>>> {
  using type = decltype(::fasync::internal::invoke_handler(std::declval<H>(),
                                                           std::declval<::fasync::context&>()));
};

// Determines the output of a handler type H chained onto a future type F.
template <typename H, typename F, requires_conditions<is_future<F>> = true>
using handler_output_t = typename handler_output<H, F>::type;

template <typename H, typename F, requires_conditions<is_try_future<F>> = true>
using value_handler_result_t =
    ::fit::result<future_error_t<F>, decltype(::fasync::internal::invoke_handler(
                                         std::declval<H>(), std::declval<::fasync::context&>(),
                                         std::declval<::fasync::future_value_t<F>>()))>;

template <typename H, typename F, requires_conditions<is_try_future<F>> = true>
using error_handler_result_t =
    ::fit::result<decltype(::fasync::internal::invoke_handler(
                      std::declval<H>(), std::declval<::fasync::context&>(),
                      std::declval<::fasync::future_error_t<F>>())),
                  ::fasync::future_value_t<F>>;

// |pending_future|
//
// A future that always returns pending. The template parameters are used for
// the returned |fasync::poll<Ts...>| type.
template <typename...>
class pending_future;

template <>
class LIB_FASYNC_NODISCARD pending_future<> final {
 public:
  // Same rules as for value_future even though we don't have an actual value
  constexpr pending_future() = default;

  constexpr pending_future(const pending_future&) = default;
  constexpr pending_future& operator=(const pending_future&) = default;
  constexpr pending_future(pending_future&&) = default;
  constexpr pending_future& operator=(pending_future&&) = default;

  constexpr ::fasync::poll<> operator()(::fasync::context&) const { return ::fasync::pending(); }
};

template <typename T>
class LIB_FASYNC_NODISCARD pending_future<T> final {
 public:
  // Same rules as for value_future even though we don't have an actual value
  constexpr pending_future() = default;

  constexpr pending_future(pending_future<>) {}

  template <typename U, requires_conditions<std::is_constructible<T, U>> = true>
  constexpr pending_future(pending_future<U>) {}

  constexpr pending_future(const pending_future&) = default;
  constexpr pending_future& operator=(const pending_future&) = default;
  constexpr pending_future(pending_future&&) = default;
  constexpr pending_future& operator=(pending_future&&) = default;

  constexpr ::fasync::poll<T> operator()(::fasync::context&) const { return ::fasync::pending(); }
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

pending_future()->pending_future<>;

template <typename T>
pending_future(pending_future<T>&&) -> pending_future<T>;

#endif

// A |pending_future| for |fit::result<E, Ts...>|.
template <typename E, typename... Ts>
using pending_try_future = pending_future<::fit::result<E, Ts...>>;

// |value_future| is a future that always resolves with a single value.
template <typename T>
class LIB_FASYNC_NODISCARD LIB_FASYNC_OWNER_OF(T) value_future final {
  using value_type = std::decay_t<T>;

 public:
  template <typename... Args,
            requires_conditions<std::is_constructible<value_type, Args...>> = true>
  explicit constexpr value_future(Args&&... args) : value_(std::forward<Args>(args)...) {}

  template <typename U, requires_conditions<std::is_constructible<value_type, U>> = true>
  constexpr value_future(value_future<U>&& other) : value_(std::forward<U>(other.value_)) {}

  constexpr value_future(const value_future&) = default;
  constexpr value_future& operator=(const value_future&) = default;
  constexpr value_future(value_future&&) = default;
  constexpr value_future& operator=(value_future&&) = default;

  constexpr ::fasync::poll<value_type> operator()(::fasync::context&) {
    return ::fasync::done(std::move(value_));
  }

 private:
  value_type value_;
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

template <typename T>
value_future(T&&) -> value_future<T>;

template <typename T>
value_future(value_future<T>&&) -> value_future<T>;

#endif

// These next futures are variants of |value_future| for |fit::result| and related types.
template <typename E, typename... Ts>
using result_future = value_future<::fit::result<std::decay_t<E>, std::decay_t<Ts>...>>;

template <typename... Ts>
using ok_future = result_future<::fit::failed, std::decay_t<Ts>...>;

template <typename E>
using error_future = result_future<std::decay_t<E>>;

using failed_future = result_future<::fit::failed>;

// Describes the status of a poller.
enum class poller_state : uint8_t {
  // The poller holds a future that may eventually produce an output but
  // it currently doesn't have an output.  The poller's future must be
  // invoked in order to make progress from this state.
  pending,
  // The poller has an output ready.
  ready,
};

// |fasync::poller|
//
// Holds onto a future until it has completed, then provides access to its
// output.
//
// SYNOPSIS
//
// |F| is the type of future the poller acts upon.
// Can be accessed via |typename poller<F>::future_type| or |poller_future_t<P>|.
//
// |typename poller<F>::output_type| is the type of output produced by the future (and thus poller).
// Can also be accessed via |poller_output_t<P>|.
//
// THEORY OF OPERATION
//
// A poller has a single owner who is responsible for setting its future
// or output and driving its execution.  Unlike |fasync::future|, a poller retains
// the output produced by completion of its asynchronous task.  Output retention
// eases the implementation of combined tasks that need to await the results
// of other tasks before proceeding.
//
// A poller can be in one of two states, depending on whether it holds...
// - an output: |poller_state::ready|
// - a future that may eventually produce a result: |poller_state::pending|
//
// On its own, a poller is "inert"; it only makes progress in response to
// actions taken by its owner. The state of the poller never changes
// spontaneously or concurrently.
//
// When the poller's state is |poller_state::pending|, its owner is
// responsible for calling the poller's |operator()| to invoke the future.
// If the future completes and returns a result, the poller will transition
// to the ready state. The future itself will
// then be destroyed since it has fulfilled its purpose.
//
// When the poller's state is |poller_state::ready|, its owner is responsible
// for consuming the stored value using |output()| or |take_output()|.
//
// See also |fasync::future| for more information about promises and their
// execution.
template <typename F>
class LIB_FASYNC_NODISCARD LIB_FASYNC_OWNER poller final {
  static_assert(is_future_v<F>, "");

 public:
  // The type of future held by the poller.
  using future_type = std::decay_t<F>;

  // The future's output type.
  using output_type = std::decay_t<::fasync::future_output_t<F>>;

  // Creates a poller and assigns a future to compute its output.
  // The poller enters the pending state.
  template <typename G, requires_conditions<std::is_constructible<future_type, G>> = true>
  explicit constexpr poller(G&& future)
      : state_(cpp17::in_place_index<0>, std::forward<G>(future)) {}

  // Creates a poller and assigns its output.
  // The poller enters the ready state.
  template <typename T = output_type,
            requires_conditions<std::is_constructible<output_type, T>> = true>
  explicit constexpr poller(T&& output)
      : state_(cpp17::in_place_index<1>, std::forward<T>(output)) {}

  constexpr poller(const poller&) = delete;
  constexpr poller& operator=(const poller&) = delete;
  constexpr poller(poller&& other) = default;
  constexpr poller& operator=(poller&&) = default;

  // Destroys the poller, releasing its future and output (if any).
  ~poller() = default;

  // Returns the state of the poller: pending or ready.
  constexpr poller_state state() const {
    switch (state_.index()) {
      case 0:
        return poller_state::pending;
      case 1:
        return poller_state::ready;
    }
    __builtin_unreachable();
  }

  // Returns true if the poller's state is |fasync::poller_state::pending|:
  // it does not hold an output yet but it does hold a future that can be invoked
  // to make progress towards obtaining an output.
  constexpr bool is_pending() const { return state() == poller_state::pending; }

  // Returns true if the poller's state is |fasync::poller_state::ready|.
  constexpr bool is_ready() const { return state() == poller_state::ready; }

  // Evaluates the poller and returns true if its output is ready.
  //
  // If the future completes and returns an output, the poller will transition
  // to the ready state. The future itself will
  // then be destroyed since it has fulfilled its purpose.
  constexpr bool operator()(fasync::context& context) {
    switch (state_.index()) {
      case 0: {
        ::fasync::future_poll_t<future_type> poll = cpp20::invoke(cpp17::get<0>(state_), context);
        if (poll.is_pending()) {
          return false;
        }
        emplace_output(std::move(poll));
        return true;
      }
      case 1:
        return true;
    }
    __builtin_unreachable();
  }

  // Gets a reference to the poller's future.
  // Asserts that the poller's state is |fasync::poller_state::pending|.
  constexpr future_type& future() {
    return const_cast<future_type&>(cpp17::as_const(*this).future());
  }

  // TODO(schottm): do other ref qualifiers
  constexpr const future_type& future() const {
    assert(is_pending());
    return cpp17::get<0>(state_);
  }

  // Takes the poller's future, leaving it in an empty state.
  // Asserts that the poller's state is |fasync::poller_state::pending|.
  constexpr future_type take_future() {
    assert(is_pending());
    return std::move(cpp17::get<0>(state_));
  }

  // Gets a reference to the poller's output.
  // Asserts that the poller's state is |fasync::poller_state::ready|.
  template <typename T = output_type, requires_conditions<cpp17::negation<std::is_void<T>>> = true>
  constexpr std::add_lvalue_reference_t<output_type> output() {
    return const_cast<std::add_lvalue_reference_t<output_type>>(cpp17::as_const(*this).output());
  }

  template <typename T = output_type, requires_conditions<cpp17::negation<std::is_void<T>>> = true>
  constexpr std::add_lvalue_reference_t<const output_type> output() const {
    assert(is_ready());
    return cpp17::get<1>(state_);
  }

  // Takes the poller's output, leaving it in an empty state.
  // Asserts that the poller's state is |fasync::poller_state::ready|.
  constexpr output_type take_output() {
    assert(is_ready());
    return std::move(cpp17::get<1>(state_));
  }

  // Assigns a future to compute the poller's output.
  // The poller enters the pending state.
  constexpr poller& operator=(future_type&& future) {
    state_.emplace<0>(std::move(future));
    return *this;
  }

  // Assigns the poller's output.
  // The poller enters the ready state.
  //
  // NOTE: the substitution failure for void&& is sufficient to guard against that usage.
  template <typename T = output_type>
  constexpr poller& operator=(T&& output) {
    state_.emplace<1>(std::move(output));
    return *this;
  }

  // Swaps the pollers' contents.
  constexpr void swap(poller& other) {
    using std::swap;
    swap(state_, other.state_);
  }

 private:
  template <typename P, requires_conditions<is_void_poll<P>> = true>
  void emplace_output(P&& poll) {
    state_.template emplace<1>(cpp17::monostate());
  }

  template <typename P, requires_conditions<cpp17::negation<is_void_poll<P>>> = true>
  void emplace_output(P&& poll) {
    state_.template emplace<1>(std::forward<P>(poll).output());
  }

  template <typename T>
  using real_or_monostate = std::conditional_t<cpp17::is_void_v<T>, cpp17::monostate, T>;

  cpp17::variant<future_type, real_or_monostate<output_type>> state_;
};

template <typename F>
using poller_future_t = typename poller<F>::future_type;

template <typename F>
using poller_output_t = typename poller<F>::output_type;

// The continuation produced by |fasync::map()|.
template <typename F, typename H>
class LIB_FASYNC_OWNER map_future final {
  static_assert(is_future_v<F>, "");

 public:
  template <typename G, typename I, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr map_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  constexpr handle_map_t<H, F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    return handle_map(handler_, context, poller_);
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using map_future_t = map_future<std::decay_t<F>, std::decay_t<H>>;

// The continuation produced by |fasync::map_ok|.
template <typename F, typename H>
class LIB_FASYNC_OWNER map_ok_future final {
  static_assert(is_try_future_v<F>, "");

 public:
  template <typename G, typename I, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr map_ok_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  constexpr handle_map_ok_t<H, F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return ::fasync::done(::fit::as_error(poller_.output().error_value()));
    }
    return handle_map_ok(handler_, context, poller_);
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using map_ok_future_t = map_ok_future<std::decay_t<F>, std::decay_t<H>>;

// The continuation produced by |fasync::map_error|.
template <typename F, typename H>
class map_error_future final {
  static_assert(is_try_future_v<F>, "");

 public:
  template <typename G, typename I, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr map_error_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  template <typename G = F, requires_conditions<is_value_try_future<G>> = true>
  constexpr handle_map_error_t<H, F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return handle_map_error(handler_, context, poller_);
    }
    return ::fasync::done(::fit::ok(std::move(poller_.output()).value()));
  }

  template <typename G = F, requires_conditions<cpp17::negation<is_value_try_future<G>>> = true>
  constexpr handle_map_error_t<H, F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return handle_map_error(handler_, context, poller_);
    }
    return ::fasync::done(::fit::ok());
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using map_error_future_t = map_error_future<std::decay_t<F>, std::decay_t<H>>;

// The continuation produced by |fasync::flatten|.
template <typename F>
class flatten_future final {
  static_assert(is_future_v<F> && is_future_v<future_output_t<F>>, "");

 public:
  template <typename G, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr flatten_future(G&& future) : poller_(std::forward<G>(future)) {}

  constexpr ::fasync::future_poll_t<::fasync::future_output_t<F>> operator()(
      ::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    return cpp20::invoke(poller_.output(), context);
  }

 private:
  poller<F> poller_;
};

template <typename F>
using flatten_future_t = flatten_future<std::decay_t<F>>;

// A continuation used to flatten futures producing a |fit::result| where the value type is a
// future. Used by |fasync::and_then|.
template <typename F>
class try_flatten_future final {
  static_assert(is_try_future_v<F>, "");

  using value_future = ::fasync::future_value_t<F>;

  template <typename G, typename = bool>
  struct poll_type {
    using type = ::fasync::try_poll<::fasync::future_error_t<G>>;
  };

  template <typename G>
  struct poll_type<G, requires_conditions<is_value_try_future<G>>> {
    using type = ::fasync::try_poll<::fasync::future_error_t<F>, ::fasync::future_value_t<G>>;
  };

  template <typename G, requires_conditions<is_try_future<G>> = true>
  using poll_t = typename poll_type<G>::type;

 public:
  template <typename G, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr try_flatten_future(G&& future) : poller_(std::forward<G>(future)) {}

  constexpr poll_t<value_future> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return ::fasync::done(::fit::as_error(poller_.output().error_value()));
    }
    return poller_.output().value()(context);
  }

 private:
  poller<F> poller_;
};

template <typename F>
using try_flatten_future_t = try_flatten_future<std::decay_t<F>>;

// A continuation used to flatten futures producing a |fit::result| where the error type is a
// future. Used by |fasync::or_else|.
template <typename F>
class try_flatten_error_future final {
  static_assert(is_try_future_v<F>, "");

  using error_future = ::fasync::future_error_t<F>;

  template <typename G, typename = bool>
  struct poll_type {
    using type = ::fasync::try_poll<::fasync::future_error_t<error_future>>;
  };

  template <typename G>
  struct poll_type<G, requires_conditions<is_value_try_future<G>>> {
    using type =
        ::fasync::try_poll<::fasync::future_error_t<error_future>, ::fasync::future_value_t<G>>;
  };

  template <typename G, requires_conditions<is_try_future<G>> = true>
  using poll_t = typename poll_type<G>::type;

 public:
  template <typename G, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  constexpr try_flatten_error_future(G&& future) : poller_(std::forward<G>(future)) {}

  template <typename G = F, requires_conditions<is_value_try_future<G>> = true>
  constexpr poll_t<F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return cpp20::invoke(poller_.output().error_value(), context);
    }
    return ::fasync::done(::fit::ok(std::move(poller_.output()).value()));
  }

  template <typename G = F, requires_conditions<cpp17::negation<is_value_try_future<G>>> = true>
  constexpr poll_t<F> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    if (poller_.output().is_error()) {
      return cpp20::invoke(poller_.output().error_value(), context);
    }
    return ::fasync::done(::fit::ok());
  }

 private:
  poller<F> poller_;
};

template <typename F>
using try_flatten_error_future_t = try_flatten_error_future<std::decay_t<F>>;

template <typename F, typename H, requires_conditions<is_future<F>> = true>
class inspect_future final {
  static_assert(!is_void_future_v<F> && handler_returns_void_for_v<H, const future_output_t<F>&>,
                "");

 public:
  template <
      typename G, typename I,
      requires_conditions<std::is_constructible<poller<F>, G>, std::is_constructible<H, I>> = true>
  explicit constexpr inspect_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  constexpr future_poll_t<F> operator()(context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    invoke_handler(handler_, context, cpp17::as_const(poller_.output()));
    return ::fasync::done(std::move(poller_.output()));
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using inspect_future_t = inspect_future<std::decay_t<F>, std::decay_t<H>>;

template <typename F, typename H, requires_conditions<is_try_future<F>> = true>
class inspect_ok_future final {
  static_assert(handler_returns_void_for_v<H, const future_value_t<F>&>, "");

 public:
  template <
      typename G, typename I,
      requires_conditions<std::is_constructible<poller<F>, G>, std::is_constructible<H, I>> = true>
  explicit constexpr inspect_ok_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  constexpr future_poll_t<F> operator()(context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    } else if (poller_.output().is_ok()) {
      invoke_handler(handler_, context, cpp17::as_const(poller_.output().value()));
    }
    return ::fasync::done(std::move(poller_.output()));
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using inspect_ok_future_t = inspect_ok_future<std::decay_t<F>, std::decay_t<H>>;

template <typename F, typename H, requires_conditions<is_try_future<F>> = true>
class inspect_error_future final {
  static_assert(handler_returns_void_for_v<H, const future_error_t<F>&>, "");

 public:
  template <
      typename G, typename I,
      requires_conditions<std::is_constructible<poller<F>, G>, std::is_constructible<H, I>> = true>
  explicit constexpr inspect_error_future(G&& future, I&& handler)
      : poller_(std::forward<G>(future)), handler_(std::forward<I>(handler)) {}

  constexpr future_poll_t<F> operator()(context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    } else if (poller_.output().is_error()) {
      invoke_handler(handler_, context, cpp17::as_const(poller_.output().error_value()));
    }
    return ::fasync::done(std::move(poller_.output()));
  }

 private:
  poller<F> poller_;
  H handler_;
};

template <typename F, typename H>
using inspect_error_future_t = inspect_error_future<std::decay_t<F>, std::decay_t<H>>;

template <typename F, requires_conditions<is_future<F>> = true>
class discard_future final {
 public:
  template <typename G, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  explicit constexpr discard_future(G&& future) : poller_(std::forward<G>(future)) {}

  constexpr ::fasync::poll<> operator()(::fasync::context& context) {
    if (!poller_(context)) {
      return ::fasync::pending();
    }
    return ::fasync::done();
  }

 private:
  poller<F> poller_;
};

template <typename F>
using discard_future_t = discard_future<std::decay_t<F>>;

template <typename... Rest>
constexpr bool and_all(bool first, Rest&&... rest) {
  return first && and_all(std::forward<Rest>(rest)...);
}

template <>
constexpr bool and_all(bool arg) {
  return arg;
}

template <typename... Fs>
class join_future final {
  static_assert(cpp17::conjunction_v<is_future<Fs>...>, "");
  using output_type = std::tuple<future_output_t<Fs>...>;
  using in_progress_type = std::tuple<::fasync::internal::poller<Fs>...>;

 public:
  template <
      typename... Gs,
      requires_conditions<std::is_constructible<::fasync::internal::poller<Fs>, Gs>...> = true>
  explicit constexpr join_future(Gs&&... futures) : in_progress_(std::forward<Gs>(futures)...) {}

  constexpr ::fasync::poll<output_type> operator()(::fasync::context& context) {
    return evaluate_join(context, std::index_sequence_for<Fs...>());
  }

 private:
  template <size_t... Is>
  constexpr ::fasync::poll<output_type> evaluate_join(::fasync::context& context,
                                                      std::index_sequence<Is...>) {
    bool all_ready = and_all(std::get<Is>(in_progress_)(context)...);
    if (all_ready) {
      return done(cpp17::apply(
          [](auto&&... pollers) { return output_type(std::move(pollers.output())...); },
          in_progress_));
    } else {
      return ::fasync::pending();
    }
  }

  in_progress_type in_progress_;
};

template <typename... Fs>
using join_future_t = join_future<std::decay_t<Fs>...>;

template <template <typename, typename> class C, typename F,
          template <typename> class Allocator = std::allocator>
class join_container_future final {
  static_assert(is_future_v<F>, "");
  using output_type = C<future_output_t<F>, Allocator<future_output_t<F>>>;
  using in_progress_type = C<poller<F>, Allocator<poller<F>>>;

 public:
  template <template <typename, typename> class D, typename G,
            template <typename> class OtherAllocator = std::allocator,
            requires_conditions<std::is_constructible<poller<F>, G>> = true>
  explicit constexpr join_container_future(D<G, OtherAllocator<G>>&& futures)
      : in_progress_(std::make_move_iterator(std::begin(futures)),
                     std::make_move_iterator(std::end(futures))) {}

  constexpr ::fasync::poll<output_type> operator()(::fasync::context& context) {
    bool all_ready = true;
    for (auto& i : in_progress_) {
      all_ready &= i(context);
    }
    if (!all_ready) {
      return ::fasync::pending();
    }
    output_type output;
    std::transform(std::begin(in_progress_), std::end(in_progress_), std::back_inserter(output),
                   [](auto&& poller) { return std::move(poller.output()); });
    return ::fasync::done(std::move(output));
  }

 private:
  in_progress_type in_progress_;
};

template <template <typename, size_t> class View, typename F, size_t N>
class join_view_future final {
  static_assert(is_future_v<F>, "");
  using output_type = std::array<future_output_t<F>, N>;
  using in_progress_type = std::array<cpp17::optional<future_output_t<F>>, N>;

 public:
  template <template <typename, size_t> class OtherView, typename G, size_t O,
            requires_conditions<cpp17::bool_constant<N != cpp20::dynamic_extent>,
                                std::is_constructible<View<F, N>, OtherView<G, O>>> = true>
  explicit constexpr join_view_future(OtherView<G, O> other) : view_(std::move(other)) {}

  constexpr ::fasync::poll<output_type> operator()(::fasync::context& context) {
    bool all_ready = true;
    auto i = std::begin(view_);
    auto j = std::begin(in_progress_);
    for (; i != std::end(view_) && j != std::end(in_progress_);
         i = std::next(i), j = std::next(j)) {
      if (j->has_value()) {
        continue;
      }
      future_poll_t<F> p = cpp20::invoke(*i, context);
      all_ready &= p.is_ready();
      if (p.is_ready()) {
        *j = std::move(p).output();
      }
    }
    if (!all_ready) {
      return ::fasync::pending();
    }
    return ::fasync::done(cpp17::apply(
        [](auto&&... opts) { return output_type{std::move(*opts)...}; }, std::move(in_progress_)));
  }

 private:
  View<F, N> view_;
  in_progress_type in_progress_;
};

template <typename F>
class join_span_future final {
  static_assert(is_future_v<F>, "");
  using output_type = std::vector<future_output_t<F>>;
  using in_progress_type = std::vector<poller<F>>;

 public:
  template <typename G, requires_conditions<std::is_constructible<poller<F>, G>> = true>
  explicit constexpr join_span_future(cpp20::span<G> span)
      : in_progress_(std::make_move_iterator(std::begin(span)),
                     std::make_move_iterator(std::end(span))) {}

  constexpr ::fasync::poll<output_type> operator()(::fasync::context& context) {
    bool all_ready = true;
    for (auto& i : in_progress_) {
      all_ready &= i(context);
    }
    if (!all_ready) {
      return ::fasync::pending();
    }
    output_type output;
    std::transform(std::begin(in_progress_), std::end(in_progress_), std::back_inserter(output),
                   [](auto&& poller) { return std::move(poller.output()); });
    return ::fasync::done(std::move(output));
  }

 private:
  in_progress_type in_progress_;
};

// An adaptor for callables returning |fasync::pending| for |fasync::make_future|.
template <typename StoredH>
class pending_adaptor final {
  using return_type = invoke_handler_t<StoredH>;
  static_assert(is_pending_v<return_type>, "");

 public:
  template <typename H, requires_conditions<std::is_constructible<StoredH, H>> = true>
  constexpr pending_adaptor(H&& handler) : handler_(std::forward<H>(handler)) {}

  constexpr ::fasync::poll<> operator()(::fasync::context& context) {
    return invoke_handler(handler_, context);
  }

 private:
  StoredH handler_;
};

template <typename H>
using pending_adaptor_t = pending_adaptor<std::decay_t<H>>;

// An adaptor for callables returning |fasync::ready| for |fasync::make_future|.
template <typename StoredH>
class ready_adaptor final {
  using return_type = invoke_handler_t<StoredH>;
  static_assert(is_ready_v<return_type>, "");
  using output_type = ready_output_t<return_type>;
  using poll_type = std::conditional_t<cpp17::is_void_v<output_type>, ::fasync::poll<>,
                                       ::fasync::poll<output_type>>;

 public:
  template <typename H, requires_conditions<std::is_constructible<StoredH, H>> = true>
  constexpr ready_adaptor(H&& handler) : handler_(std::forward<H>(handler)) {}

  constexpr poll_type operator()(::fasync::context& context) {
    return invoke_handler(handler_, context);
  }

 private:
  StoredH handler_;
};

template <typename H>
using ready_adaptor_t = ready_adaptor<std::decay_t<H>>;

// An adaptor for callables returning another future for |fasync::make_future|.
template <typename StoredH>
class future_adaptor final {
  using future_type = invoke_handler_t<StoredH>;
  static_assert(is_future_v<future_type>, "");

 public:
  template <typename H,
            requires_conditions<std::is_constructible<StoredH, H>,
                                std::is_constructible<future_type, invoke_handler_t<H>>> = true>
  constexpr future_adaptor(H&& handler) : handler_(std::forward<H>(handler)) {}

  constexpr ::fasync::future_poll_t<future_type> operator()(::fasync::context& context) {
    if (!future_.has_value()) {
      move_construct_optional(future_, invoke_handler(std::move(handler_), context));
    }
    return cpp20::invoke(*future_, context);
  }

 private:
  StoredH handler_;
  cpp17::optional<future_type> future_;
};

template <typename H>
using future_adaptor_t = future_adaptor<std::decay_t<H>>;

// An adaptor for arbitrary handlers for |fasync::make_future|.
template <typename StoredH>
class handler_adaptor final {
 public:
  template <typename H, requires_conditions<std::is_constructible<StoredH, H>> = true>
  constexpr handler_adaptor(H&& handler) : handler_(std::forward<H>(handler)) {}

  constexpr auto operator()(::fasync::context& context) { return invoke(handler_, context); }

 private:
  template <typename H, requires_conditions<handler_returns_void_for<H>> = true>
  constexpr ::fasync::poll<> invoke(priority_tag<1>, H&& handler, ::fasync::context& context) {
    invoke_handler(std::forward<H>(handler), context);
    return ::fasync::done();
  }

  template <typename H, requires_conditions<cpp17::negation<handler_returns_void_for<H>>> = true>
  constexpr auto invoke(priority_tag<0>, H&& handler, ::fasync::context& context) {
    return to_poll(handle_output(std::forward<H>(handler), context));
  }

  template <typename H>
  constexpr auto invoke(H&& handler, ::fasync::context& context) {
    return invoke(priority_tag<1>(), std::forward<H>(handler), context);
  }

  StoredH handler_;
};

template <typename H>
using handler_adaptor_t = handler_adaptor<std::decay_t<H>>;

template <typename H, typename return_type = invoke_handler_t<std::decay_t<H>>>
using make_future_adaptor_t = std::conditional_t<
    is_pending_v<return_type>, pending_adaptor_t<H>,
    std::conditional_t<
        is_ready_v<return_type>, ready_adaptor_t<H>,
        std::conditional_t<is_future_v<return_type>, future_adaptor_t<H>, handler_adaptor_t<H>>>>;

// A perfect forwarding call wrapper ([func.require] § 4) for composing two callables, with the same
// call pattern as calling |F| on its own. This is useful for composing the right side of a pipeline
// ahead of time and then invoking it with a future on the left.
template <typename F, typename G>
class compose_wrapper {
 public:
  template <typename H, typename I,
            requires_conditions<std::is_constructible<F, H>, std::is_constructible<G, I>> = true>
  explicit constexpr compose_wrapper(H&& h, I&& i)
      : f_(std::forward<H>(h)), g_(std::forward<I>(i)) {}

  constexpr compose_wrapper(const compose_wrapper&) = default;
  constexpr compose_wrapper& operator=(const compose_wrapper&) = default;
  constexpr compose_wrapper(compose_wrapper&&) = default;
  constexpr compose_wrapper& operator=(compose_wrapper&&) = default;

  // TODO(schottm): noexcept and decltype for this
  template <typename... Args>
  constexpr decltype(auto) operator()(Args&&... args) & {
    return cpp20::invoke(f_, cpp20::invoke(g_, std::forward<Args>(args)...));
  }

  template <typename... Args>
  constexpr decltype(auto) operator()(Args&&... args) const& {
    return cpp20::invoke(f_, cpp20::invoke(g_, std::forward<Args>(args)...));
  }

  template <typename... Args>
  constexpr decltype(auto) operator()(Args&&... args) && {
    return cpp20::invoke(std::move(f_), cpp20::invoke(std::move(g_), std::forward<Args>(args)...));
  }

  // TODO(schottm): invocable requirements
  template <typename... Args>
  constexpr decltype(auto) operator()(Args&&... args) const&& {
    return cpp20::invoke(std::move(f_),
                         cpp20::invoke(std::move > (g_), std::forward<Args>(args)...));
  }

 private:
  F f_;
  G g_;
};

template <typename F, typename G>
using compose_wrapper_t = compose_wrapper<std::decay_t<F>, std::decay_t<G>>;

template <typename Closure>
struct future_adaptor_closure;

template <typename T>
struct is_future_adaptor_closure : std::is_base_of<future_adaptor_closure<cpp20::remove_cvref_t<T>>,
                                                   cpp20::remove_cvref_t<T>>::type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_future_adaptor_closure_v =
    is_future_adaptor_closure<T>::value;

template <typename F>
struct future_adaptor_closure_t : F, future_adaptor_closure<future_adaptor_closure_t<F>> {
  explicit constexpr future_adaptor_closure_t(F&& f) : F(std::forward<F>(f)) {}
};

}  // namespace internal
}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_FUTURE_H_
