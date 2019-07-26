// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_PROMISE_INTERNAL_H_
#define LIB_FIT_PROMISE_INTERNAL_H_

#include <assert.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "function.h"
#include "nullable.h"
#include "result.h"
#include "traits.h"
#include "utility_internal.h"

namespace fit {

template <typename Continuation>
class promise_impl;

template <typename Promise>
class future_impl;

class context;
class executor;
class suspended_task;

namespace internal {

// Determines whether a type is a kind of fit::result.
template <typename Result>
struct is_result : std::false_type {};
template <typename V, typename E>
struct is_result<::fit::result<V, E>> : std::true_type {};

// Deduces a continuation's result.
// Also ensures that the continuation has a compatible signature.
template <typename Continuation,
          typename = std::enable_if_t<is_result<decltype(
              std::declval<Continuation&>()(std::declval<::fit::context&>()))>::value>>
struct continuation_traits {
  using type = Continuation;
  using result_type = decltype(std::declval<Continuation&>()(std::declval<::fit::context&>()));
};
template <typename Continuation, typename = fit::void_t<>>
struct is_continuation : std::false_type {};
template <typename Continuation>
struct is_continuation<Continuation, fit::void_t<typename continuation_traits<Continuation>::type>>
    : std::true_type {};

// Interposer type that provides uniform move construction/assignment for
// callable types that may or may not be move assignable. Lambdas are not
// typically move assignable, even though they may be move constructible.
//
// This type has a well-defined empty state. Instances of this type that are
// the source of move operation are left in the empty state.
template <typename Handler>
class movable_handler {
  static_assert(std::is_move_constructible<Handler>::value, "Handler must be move constructible!");

  template <typename... Conditions>
  using requires_conditions = ::fit::internal::requires_conditions<Conditions...>;

  template <typename... Conditions>
  using assignment_requires_conditions =
      ::fit::internal::assignment_requires_conditions<movable_handler&, Conditions...>;

  template <typename U>
  using not_self_type = ::fit::internal::not_same_type<movable_handler, U>;

 public:
  constexpr movable_handler() = default;

  constexpr movable_handler(const movable_handler&) = delete;
  constexpr movable_handler& operator=(const movable_handler&) = delete;

  constexpr movable_handler(movable_handler&& other) : handler_{std::move(other.handler_)} {
    other.handler_.reset();
  }

  constexpr movable_handler& operator=(movable_handler&& other) {
    if (this != &other) {
      reset();
      if (other.handler_.has_value()) {
        handler_.emplace(std::move(other.handler_.value()));
        other.handler_.reset();
      }
    }
    return *this;
  }

  template <typename U = Handler,
            requires_conditions<not_self_type<U>, std::is_constructible<Handler, U&&>,
                                std::is_convertible<U&&, Handler>> = true>
  constexpr movable_handler(U&& handler) {
    if (!is_null(handler)) {
      handler_.emplace(std::forward<U>(handler));
    }
  }

  ~movable_handler() = default;

  template <typename U>
  constexpr assignment_requires_conditions<not_self_type<U>, std::is_constructible<Handler, U>,
                                           std::is_assignable<Handler&, U>>
  operator=(U&& handler) {
    handler_.reset();
    if (!is_null(handler)) {
      handler_.emplace(std::forward<U>(handler));
    }
    return *this;
  }

  template <typename... Args>
  constexpr auto operator()(Args&&... args) {
    // Seamlessly handle void by casting call expression to return type.
    using Return = typename callable_traits<Handler>::return_type;
    return static_cast<Return>((handler_.value())(std::forward<Args>(args)...));
  }

  explicit constexpr operator bool() const { return handler_.has_value(); }

  constexpr void reset() { handler_.reset(); }

 private:
  optional<Handler> handler_;
};

// Wraps a handler function and adapts its return type to a fit::result
// via its specializations.
template <typename Handler, typename DefaultV, typename DefaultE,
          typename ReturnType = typename callable_traits<Handler>::return_type,
          bool callable_result = ::fit::is_callable<ReturnType>::value>
class result_adapter final {
  // This expression always evaluates to false but depends on the template
  // type parameters so that it only gets evaluated when the template is
  // expanded.  If we simply wrote "false", the compiler would raise the
  // static assertion failure as soon as it encountered the statement.
  template <typename T>
  struct check_result {
    static constexpr bool value = false;
  };
  static_assert(check_result<ReturnType>::value,
                "The provided handler's result type was expected to be "
                "fit::result<V, E>, fit::ok_result<V>, fit::error_result<E>, "
                "fit::pending_result, void, or a continuation with the signature "
                "fit::result<V, E>(fit::context&).  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");
};

// Supports handlers that return void.
template <typename Handler, typename DefaultV, typename DefaultE>
class result_adapter<Handler, DefaultV, DefaultE, void, false> final {
 public:
  using result_type = ::fit::result<DefaultV, DefaultE>;

  explicit result_adapter(Handler handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    handler_(std::forward<Args>(args)...);
    return ::fit::ok();
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
};

// Supports handlers that return pending_result.
template <typename Handler, typename DefaultV, typename DefaultE>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::pending_result, false> final {
 public:
  using result_type = ::fit::result<DefaultV, DefaultE>;

  explicit result_adapter(movable_handler<Handler> handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return handler_(std::forward<Args>(args)...);
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
};

// Supports handlers that return ok_result<V>.
template <typename Handler, typename DefaultV, typename DefaultE, typename V>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::ok_result<V>, false> final {
 public:
  using result_type = ::fit::result<V, DefaultE>;

  explicit result_adapter(movable_handler<Handler> handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return handler_(std::forward<Args>(args)...);
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
};

// Supports handlers that return error_result<E>.
template <typename Handler, typename DefaultV, typename DefaultE, typename E>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::error_result<E>, false> final {
 public:
  using result_type = ::fit::result<DefaultV, E>;

  explicit result_adapter(movable_handler<Handler> handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return handler_(std::forward<Args>(args)...);
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
};

// Supports handlers that return result<V, E>.
template <typename Handler, typename DefaultV, typename DefaultE, typename V, typename E>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::result<V, E>, false> final {
 public:
  using result_type = ::fit::result<V, E>;

  explicit result_adapter(movable_handler<Handler> handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return handler_(std::forward<Args>(args)...);
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
};

// Supports handlers that return continuations or promises.
// This works for any callable whose signature is:
//     fit::result<...>(fit::context&)
template <typename Handler, typename DefaultV, typename DefaultE, typename ReturnType>
class result_adapter<Handler, DefaultV, DefaultE, ReturnType, true> final {
  // If the handler doesn't actually return a continuation then the
  // compilation will fail here which is slightly easier to diagnose
  // than if we dropped the result_adapter specialization entirely.
  using result_continuation_traits = continuation_traits<ReturnType>;
  using continuation_type = typename result_continuation_traits::type;

 public:
  using result_type = typename result_continuation_traits::result_type;

  explicit result_adapter(movable_handler<Handler> handler) : handler_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    if (handler_) {
      continuation_ = handler_(std::forward<Args>(args)...);
      handler_.reset();
    }
    if (!continuation_) {
      return ::fit::pending();
    }
    return continuation_(context);
  }

  result_adapter(const result_adapter&) = delete;
  result_adapter& operator=(const result_adapter&) = delete;

  result_adapter(result_adapter&&) = default;
  result_adapter& operator=(result_adapter&&) = default;

 private:
  movable_handler<Handler> handler_;
  movable_handler<continuation_type> continuation_;
};

// Wraps a handler that may or may not have a fit::context& as first argument.
// This is determined by checking the argument count.
template <typename Handler, typename DefaultV, typename DefaultE, size_t num_args = 0,
          int excess_args = (static_cast<int>(::fit::callable_traits<Handler>::args::size) -
                             static_cast<int>(num_args))>
class context_adapter final {
  static_assert(excess_args >= 0,
                "The provided handler has too few arguments.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");
  static_assert(excess_args <= 1,
                "The provided handler has too many arguments.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");
};

// Supports handlers without a context argument.
template <typename Handler, typename DefaultV, typename DefaultE, size_t num_args>
class context_adapter<Handler, DefaultV, DefaultE, num_args, 0> final {
  using base_type = result_adapter<Handler, DefaultV, DefaultE>;

 public:
  using result_type = typename base_type::result_type;
  static constexpr size_t next_arg_index = 0;

  explicit context_adapter(Handler handler) : base_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return base_.template call<Args...>(context, std::forward<Args>(args)...);
  }

 private:
  base_type base_;
};

// Supports handlers with a context argument.
template <typename Handler, typename DefaultV, typename DefaultE, size_t num_args>
class context_adapter<Handler, DefaultV, DefaultE, num_args, 1> final {
  using base_type = result_adapter<Handler, DefaultV, DefaultE>;
  using context_arg_type = typename ::fit::callable_traits<Handler>::args::template at<0>;
  static_assert(std::is_same<context_arg_type, ::fit::context&>::value,
                "The provided handler's first argument was expected to be of type "
                "fit::context& based on the number of arguments it has.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");

 public:
  using result_type = typename base_type::result_type;
  static constexpr size_t next_arg_index = 1;

  explicit context_adapter(Handler handler) : base_(std::move(handler)) {}

  template <typename... Args>
  result_type call(::fit::context& context, Args... args) {
    return base_.template call<::fit::context&, Args...>(context, context,
                                                         std::forward<Args>(args)...);
  }

 private:
  base_type base_;
};

// Wraps a handler that may accept a context argument.
template <typename Handler>
class context_handler_invoker final {
  using base_type = context_adapter<Handler, void, void, 0>;

 public:
  using result_type = typename base_type::result_type;

  explicit context_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context) { return base_.template call<>(context); }

 private:
  base_type base_;
};

// Wraps a handler that may accept a context and result argument.
template <typename Handler, typename PriorResult>
class result_handler_invoker final {
  using base_type = context_adapter<Handler, void, void, 1>;
  using result_arg_type =
      typename ::fit::callable_traits<Handler>::args::template at<base_type::next_arg_index>;
  static_assert(std::is_same<result_arg_type, PriorResult&>::value ||
                    std::is_same<result_arg_type, const PriorResult&>::value,
                "The provided handler's last argument was expected to be of type "
                "fit::result<V, E>& or const fit::result<V, E>& where V is the prior "
                "result's value type and E is the prior result's error type.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");

 public:
  using result_type = typename base_type::result_type;

  explicit result_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context, PriorResult& result) {
    return base_.template call<PriorResult&>(context, result);
  }

 private:
  base_type base_;
};

// Wraps a handler that may accept a context and value argument.
template <typename Handler, typename PriorResult, typename V = typename PriorResult::value_type>
class value_handler_invoker final {
  using base_type = context_adapter<Handler, void, typename PriorResult::error_type, 1>;
  using value_arg_type =
      typename ::fit::callable_traits<Handler>::args::template at<base_type::next_arg_index>;
  static_assert(std::is_same<value_arg_type, V&>::value ||
                    std::is_same<value_arg_type, const V&>::value,
                "The provided handler's last argument was expected to be of type "
                "V& or const V& where V is the prior result's value type.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");

 public:
  using result_type = typename base_type::result_type;

  explicit value_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context, PriorResult& result) {
    return base_.template call<V&>(context, result.value());
  }

 private:
  base_type base_;
};

// Specialization for void value.
template <typename Handler, typename PriorResult>
class value_handler_invoker<Handler, PriorResult, void> final {
  using base_type = context_adapter<Handler, void, typename PriorResult::error_type, 0>;

 public:
  using result_type = typename base_type::result_type;

  explicit value_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context, PriorResult& result) {
    return base_.template call<>(context);
  }

 private:
  base_type base_;
};

// Wraps a handler that may accept a context and error argument.
template <typename Handler, typename PriorResult, typename E = typename PriorResult::error_type>
class error_handler_invoker final {
  using base_type = context_adapter<Handler, typename PriorResult::value_type, void, 1>;
  using error_arg_type =
      typename ::fit::callable_traits<Handler>::args::template at<base_type::next_arg_index>;
  static_assert(std::is_same<error_arg_type, E&>::value ||
                    std::is_same<error_arg_type, const E&>::value,
                "The provided handler's last argument was expected to be of type "
                "E& or const E& where E is the prior result's error type.  "
                "Please refer to the combinator's documentation for a list of "
                "supported handler function signatures.");

 public:
  using result_type = typename base_type::result_type;

  explicit error_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context, PriorResult& result) {
    return base_.template call<E&>(context, result.error());
  }

 private:
  base_type base_;
};

// Specialization for void error.
template <typename Handler, typename PriorResult>
class error_handler_invoker<Handler, PriorResult, void> final {
  using base_type = context_adapter<Handler, typename PriorResult::value_type, void, 0>;

 public:
  using result_type = typename base_type::result_type;

  explicit error_handler_invoker(Handler handler) : base_(std::move(handler)) {}

  result_type operator()(::fit::context& context, PriorResult& result) {
    return base_.template call<>(context);
  }

 private:
  base_type base_;
};

// The continuation produced by |fit::promise::then()|.
template <typename PriorPromise, typename ResultHandler>
class then_continuation final {
  using invoker_type =
      ::fit::internal::result_handler_invoker<ResultHandler, typename PriorPromise::result_type>;

 public:
  then_continuation(PriorPromise prior_promise, ResultHandler handler)
      : prior_(std::move(prior_promise)), invoker_(std::move(handler)) {}

  typename invoker_type::result_type operator()(::fit::context& context) {
    if (!prior_(context))
      return ::fit::pending();
    return invoker_(context, prior_.result());
  }

 private:
  future_impl<PriorPromise> prior_;
  invoker_type invoker_;
};

// The continuation produced by |fit::promise::and_then()|.
template <typename PriorPromise, typename ValueHandler>
class and_then_continuation final {
  using invoker_type =
      ::fit::internal::value_handler_invoker<ValueHandler, typename PriorPromise::result_type>;

 public:
  and_then_continuation(PriorPromise prior_promise, ValueHandler handler)
      : prior_(std::move(prior_promise)), invoker_(std::move(handler)) {}

  typename invoker_type::result_type operator()(::fit::context& context) {
    if (!prior_(context))
      return ::fit::pending();
    if (prior_.is_error())
      return prior_.take_error_result();
    return invoker_(context, prior_.result());
  }

 private:
  future_impl<PriorPromise> prior_;
  invoker_type invoker_;
};

// The continuation produced by |fit::promise::or_else()|.
template <typename PriorPromise, typename ErrorHandler>
class or_else_continuation final {
  using invoker_type =
      ::fit::internal::error_handler_invoker<ErrorHandler, typename PriorPromise::result_type>;

 public:
  or_else_continuation(PriorPromise prior_promise, ErrorHandler handler)
      : prior_(std::move(prior_promise)), invoker_(std::move(handler)) {}

  typename invoker_type::result_type operator()(::fit::context& context) {
    if (!prior_(context))
      return ::fit::pending();
    if (prior_.is_ok())
      return prior_.take_ok_result();
    return invoker_(context, prior_.result());
  }

 private:
  future_impl<PriorPromise> prior_;
  invoker_type invoker_;
};

// The continuation produced by |fit::promise::inspect()|.
template <typename PriorPromise, typename InspectHandler>
class inspect_continuation final {
  using invoker_type =
      ::fit::internal::result_handler_invoker<InspectHandler, typename PriorPromise::result_type>;

 public:
  inspect_continuation(PriorPromise prior_promise, InspectHandler handler)
      : prior_(std::move(prior_promise)), invoker_(std::move(handler)) {}

  typename PriorPromise::result_type operator()(::fit::context& context) {
    typename PriorPromise::result_type result = prior_(context);
    if (result)
      invoker_(context, result);
    return result;
  }

 private:
  PriorPromise prior_;
  invoker_type invoker_;
};

// The continuation produced by |fit::promise::discard_result()|.
template <typename PriorPromise>
class discard_result_continuation final {
 public:
  explicit discard_result_continuation(PriorPromise prior_promise)
      : prior_(std::move(prior_promise)) {}

  fit::result<> operator()(::fit::context& context) {
    if (!prior_(context))
      return ::fit::pending();
    return ::fit::ok();
  }

 private:
  PriorPromise prior_;
};

// The continuation produced by |make_promise()|.
// This turns out to be equivalent to a context handler invoker.
template <typename PromiseHandler>
using promise_continuation = context_handler_invoker<PromiseHandler>;

// The continuation produced by |make_result_promise()|.
template <typename V, typename E>
class result_continuation final {
 public:
  explicit result_continuation(::fit::result<V, E> result) : result_(std::move(result)) {}

  ::fit::result<V, E> operator()(::fit::context& context) { return std::move(result_); }

 private:
  ::fit::result<V, E> result_;
};

// Returns true if all arguments are true or if there are none.
inline bool all_true() { return true; }
template <typename... Ts>
inline bool all_true(bool value, Ts... values) {
  return value & all_true(values...);
}

// The continuation produced by |join_promises()|.
template <typename... Promises>
class join_continuation final {
 public:
  explicit join_continuation(Promises... promises)
      : promises_(std::make_tuple(std::move(promises)...)) {}

  ::fit::result<std::tuple<typename Promises::result_type...>> operator()(::fit::context& context) {
    return evaluate(context, std::index_sequence_for<Promises...>{});
  }

 private:
  template <size_t... i>
  ::fit::result<std::tuple<typename Promises::result_type...>> evaluate(::fit::context& context,
                                                                        std::index_sequence<i...>) {
    bool done = all_true(std::get<i>(promises_)(context)...);
    if (!done)
      return ::fit::pending();
    return ::fit::ok(std::make_tuple(std::get<i>(promises_).take_result()...));
  }

  std::tuple<future_impl<Promises>...> promises_;
};

// The continuation produced by |join_promise_vector()|.
template <typename Promise>
class join_vector_continuation final {
  using promise_vector = std::vector<Promise>;
  using result_vector = std::vector<typename Promise::result_type>;

 public:
  explicit join_vector_continuation(promise_vector promises)
      : promises_(std::move(promises)), results_(promises_.size()) {}

  ::fit::result<result_vector> operator()(::fit::context& context) {
    bool all_done{true};
    for (size_t i = 0; i < promises_.size(); ++i) {
      if (!results_[i]) {
        results_[i] = promises_[i](context);
        all_done &= !results_[i].is_pending();
      }
    }
    if (all_done) {
      return fit::ok(std::move(results_));
    }
    return ::fit::pending();
  }

 private:
  promise_vector promises_;
  result_vector results_;
};

}  // namespace internal

template <typename PromiseHandler>
inline promise_impl<::fit::internal::promise_continuation<PromiseHandler>> make_promise(
    PromiseHandler handler);

template <typename Continuation>
inline promise_impl<Continuation> make_promise_with_continuation(Continuation continuation);

}  // namespace fit

#endif  // LIB_FIT_PROMISE_INTERNAL_H_
