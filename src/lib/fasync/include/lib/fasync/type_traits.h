// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_TYPE_TRAITS_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_TYPE_TRAITS_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fitx/result.h>

namespace fasync {

namespace internal {

using ::fit::internal::requires_conditions;

using ::fit::internal::is_result;
using ::fit::internal::is_result_v;

using ::fit::internal::is_match;
using ::fit::internal::is_match_v;

}  // namespace internal

struct pending;

template <typename...>
class ready;

template <typename...>
class poll;

class context;

// Detects whether the given type is an |fasync::pending|.
template <typename P>
struct is_pending : std::is_convertible<P, const pending&>::type {};

template <typename P>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_pending_v = is_pending<P>::value;

// Detects whether the given type is an |fasync::ready|.
template <typename T>
struct is_ready : ::fasync::internal::is_match<T, ::fasync::ready>::type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_ready_v = is_ready<T>::value;

// Detects whether the given type is an |fasync::poll|.
template <typename T>
struct is_poll : ::fasync::internal::is_match<T, ::fasync::poll>::type {};

template <typename T>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_poll_v = is_poll<T>::value;

// This trait is at the core of what makes futures work. All futures are callables with a single
// parameter that is an |fasync::context&|, and return an |fasync::poll<Ts...>|. |fasync::poll| is
// defined in |poll.h|.
template <typename F, typename = bool>
struct is_future : std::false_type {};

template <typename F>
struct is_future<
    F, ::fasync::internal::requires_conditions<is_poll<cpp17::invoke_result_t<F, context&>>>>
    : std::true_type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_future_v = is_future<F>::value;

// All executors inherit from |fasync::executor|, which provides a |schedule()| method, but this
// trait can be helpful since executors can also provide other overloads of that method, for example
// for more specific types of futures.
class executor;

// Detects whether the given type is usable as an executor of |fasync::future|s.
template <typename E>
struct is_executor : std::is_base_of<executor, cpp20::remove_cvref_t<E>>::type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_executor_v = is_executor<F>::value;

// These metafunctions (and others below) are the primary way to query type information about
// futures and related types. Many of them SFINAE where appropriate to aid in overload resolution.

// Retrieves the |::output_type| of an |fasync::ready|.
template <typename R, ::fasync::internal::requires_conditions<is_ready<R>> = true>
using ready_output_t = typename R::output_type;

// Retrieves the |::output_type| of an |fasync::poll|.
template <typename P, ::fasync::internal::requires_conditions<is_poll<P>> = true>
using poll_output_t = typename P::output_type;

// Retrieves the |fasync::poll| type returned by an |fasync::future|.
template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
using future_poll_t = cpp17::invoke_result_t<F, context&>;

// Retrieves the output type returned inside an |fasync::poll| returned by a future.
template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
using future_output_t = poll_output_t<future_poll_t<F>>;

// Detects whether an |fasync::poll|'s |::output_type| is |void|.
template <typename P>
struct is_void_poll
    : cpp17::conjunction<is_poll<P>, std::is_void<::fasync::poll_output_t<P>>>::type {};

template <typename P>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_void_poll_v = is_void_poll<P>::value;

// Detects whether the given type is an |fasync::poll<fitx::result<E, Ts...>>|, aka
// |fasync::try_poll<E, Ts...>|.
template <typename P>
struct is_try_poll
    : cpp17::conjunction<is_poll<P>,
                         ::fasync::internal::is_result<::fasync::poll_output_t<P>>>::type {};

template <typename P>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_try_poll_v = is_try_poll<P>::value;

// Detects whether the given type is a future with a |void| output.
template <typename F>
struct is_void_future
    : cpp17::conjunction<is_future<F>, std::is_void<::fasync::future_output_t<F>>>::type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_void_future_v = is_void_future<F>::value;

// Detects whether the given type is a future with an output of |fitx::result|.
template <typename F>
struct is_try_future
    : cpp17::conjunction<is_future<F>,
                         ::fasync::internal::is_result<::fasync::future_output_t<F>>>::type {};

template <typename F>
LIB_FASYNC_INLINE_CONSTANT constexpr bool is_try_future_v = is_try_future<F>::value;

// Retrieves the |::error_type| of a |fitx::result|.
template <typename R,
          ::fasync::internal::requires_conditions<::fasync::internal::is_result<R>> = true>
using result_error_t = typename R::error_type;

// Retrieves the |::value_type| of a |fitx::result|.
template <typename R,
          ::fasync::internal::requires_conditions<::fasync::internal::is_result<R>> = true>
using result_value_t = typename R::value_type;

// Retrieves the |::output_type| of an |fasync::poll| only when that type is a |fitx::result|.
template <typename P, ::fasync::internal::requires_conditions<is_try_poll<P>> = true>
using poll_result_t = poll_output_t<P>;

// Retrieves the |::value_type| of the |fitx::result| of the given |fasync::try_poll|.
template <typename P, ::fasync::internal::requires_conditions<is_try_poll<P>> = true>
using poll_value_t = result_value_t<poll_result_t<P>>;

// Retrieves the |::error_type| of the |fitx::result| of the given |fasync::try_poll|.
template <typename P, ::fasync::internal::requires_conditions<is_try_poll<P>> = true>
using poll_error_t = result_error_t<poll_result_t<P>>;

// Retrieves the output type of the given future only when that type is a |fitx::result|.
template <typename F, ::fasync::internal::requires_conditions<is_try_future<F>> = true>
using future_result_t = future_output_t<F>;

// Retrieves the |::value_type| of the |fitx::result| output by the given future.
template <typename F, ::fasync::internal::requires_conditions<is_try_future<F>> = true>
using future_value_t = result_value_t<future_result_t<F>>;

// Retrieves the |::error_type| of the |fitx::result| output by the given future.
template <typename F, ::fasync::internal::requires_conditions<is_try_future<F>> = true>
using future_error_t = result_error_t<future_result_t<F>>;

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_TYPE_TRAITS_H_
