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
          typename = std::enable_if_t<is_result<
              decltype(std::declval<Continuation&>()(
                  std::declval<::fit::context&>()))>::value>>
struct continuation_traits {
    using type = Continuation;
    using result_type = decltype(std::declval<Continuation&>()(std::declval<::fit::context&>()));
};
template <typename Continuation, typename = fit::void_t<>>
struct is_continuation : std::false_type {};
template <typename Continuation>
struct is_continuation<
    Continuation,
    fit::void_t<typename continuation_traits<Continuation>::type>>
    : std::true_type {};

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
    struct check_result { static constexpr bool value = false; };
    static_assert(
        check_result<ReturnType>::value,
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

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        handler_(std::forward<Args>(args)...);
        return ::fit::ok();
    }

private:
    Handler handler_;
};

// Supports handlers that return pending_result.
template <typename Handler, typename DefaultV, typename DefaultE>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::pending_result, false> final {
public:
    using result_type = ::fit::result<DefaultV, DefaultE>;

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return handler_(std::forward<Args>(args)...);
    }

private:
    Handler handler_;
};

// Supports handlers that return ok_result<V>.
template <typename Handler, typename DefaultV, typename DefaultE,
          typename V>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::ok_result<V>, false> final {
public:
    using result_type = ::fit::result<V, DefaultE>;

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return handler_(std::forward<Args>(args)...);
    }

private:
    Handler handler_;
};

// Supports handlers that return error_result<E>.
template <typename Handler, typename DefaultV, typename DefaultE,
          typename E>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::error_result<E>, false> final {
public:
    using result_type = ::fit::result<DefaultV, E>;

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return handler_(std::forward<Args>(args)...);
    }

private:
    Handler handler_;
};

// Supports handlers that return result<V, E>.
template <typename Handler, typename DefaultV, typename DefaultE,
          typename V, typename E>
class result_adapter<Handler, DefaultV, DefaultE, ::fit::result<V, E>, false> final {
public:
    using result_type = ::fit::result<V, E>;

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return handler_(std::forward<Args>(args)...);
    }

private:
    Handler handler_;
};

// Supports handlers that return continuations or promises.
// This works for any callable whose signature is:
//     fit::result<...>(fit::context&)
template <typename Handler, typename DefaultV, typename DefaultE,
          typename ReturnType>
class result_adapter<Handler, DefaultV, DefaultE, ReturnType, true> final {
    // If the handler doesn't actually return a continuation then the
    // compilation will fail here which is slightly easier to diagnose
    // than if we dropped the result_adapter specialization entirely.
    using continuation_traits = continuation_traits<ReturnType>;
    using continuation_type = typename continuation_traits::type;

public:
    using result_type = typename continuation_traits::result_type;

    explicit result_adapter(Handler handler)
        : handler_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        if (handler_) {
            continuation_ = (*handler_)(std::forward<Args>(args)...);
            handler_.reset();
        }
        if (!continuation_) {
            return ::fit::pending();
        }
        return (*continuation_)(context);
    }

private:
    ::fit::nullable<Handler> handler_;
    ::fit::nullable<continuation_type> continuation_;
};

// Wraps a handler that may or may not have a fit::context& as first argument.
// This is determined by checking the argument count.
template <typename Handler, typename DefaultV, typename DefaultE,
          size_t num_args = 0,
          int excess_args =
              (static_cast<int>(
                   ::fit::callable_traits<Handler>::args::size) -
               static_cast<int>(num_args))>
class context_adapter final {
    static_assert(
        excess_args >= 0,
        "The provided handler has too few arguments.  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");
    static_assert(
        excess_args <= 1,
        "The provided handler has too many arguments.  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");
};

// Supports handlers without a context argument.
template <typename Handler, typename DefaultV, typename DefaultE,
          size_t num_args>
class context_adapter<Handler, DefaultV, DefaultE, num_args, 0> final {
    using base_type = result_adapter<Handler, DefaultV, DefaultE>;

public:
    using result_type = typename base_type::result_type;
    static constexpr size_t next_arg_index = 0;

    explicit context_adapter(Handler handler)
        : base_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return base_.template call<Args...>(context, std::forward<Args>(args)...);
    }

private:
    base_type base_;
};

// Supports handlers with a context argument.
template <typename Handler, typename DefaultV, typename DefaultE,
          size_t num_args>
class context_adapter<Handler, DefaultV, DefaultE, num_args, 1> final {
    using base_type = result_adapter<Handler, DefaultV, DefaultE>;
    using context_arg_type =
        typename ::fit::callable_traits<Handler>::args::template at<0>;
    static_assert(
        std::is_same<context_arg_type, ::fit::context&>::value,
        "The provided handler's first argument was expected to be of type "
        "fit::context& based on the number of arguments it has.  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");

public:
    using result_type = typename base_type::result_type;
    static constexpr size_t next_arg_index = 1;

    explicit context_adapter(Handler handler)
        : base_(std::move(handler)) {}

    template <typename... Args>
    result_type call(::fit::context& context, Args... args) {
        return base_.template call<::fit::context&, Args...>(
            context, context, std::forward<Args>(args)...);
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

    explicit context_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context) {
        return base_.template call<>(context);
    }

private:
    base_type base_;
};

// Wraps a handler that may accept a context and result argument.
template <typename Handler, typename PriorResult>
class result_handler_invoker final {
    using base_type = context_adapter<Handler, void, void, 1>;
    using result_arg_type =
        typename ::fit::callable_traits<Handler>::args::template at<
            base_type::next_arg_index>;
    static_assert(
        (std::is_same<result_arg_type, PriorResult>::value &&
         std::is_copy_constructible<result_arg_type>::value) ||
            std::is_same<result_arg_type, PriorResult&>::value ||
            std::is_same<result_arg_type, const PriorResult&>::value,
        "The provided handler's last argument was expected to be of type "
        "fit::result<V, E>&, const fit::result<V, E>&, or fit::result<V, E> "
        "(if the result is copy-constructible).  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");

public:
    using result_type = typename base_type::result_type;

    explicit result_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context, PriorResult& result) {
        return base_.template call<PriorResult&>(context, result);
    }

private:
    base_type base_;
};

// Wraps a handler that may accept a context and value argument.
template <typename Handler, typename PriorResult,
          typename V = typename PriorResult::value_type>
class value_handler_invoker final {
    using base_type = context_adapter<Handler,
                                      void, typename PriorResult::error_type, 1>;
    using value_arg_type =
        typename ::fit::callable_traits<Handler>::args::template at<
            base_type::next_arg_index>;
    static_assert(
        (std::is_same<value_arg_type, V>::value &&
         std::is_copy_constructible<value_arg_type>::value) ||
            std::is_same<value_arg_type, V&>::value ||
            std::is_same<value_arg_type, const V&>::value,
        "The provided handler's last argument was expected to be of type "
        "V&, const V&, or V (if the value is copy-constructible).  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");

public:
    using result_type = typename base_type::result_type;

    explicit value_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context, PriorResult& result) {
        return base_.template call<V&>(context, result.value());
    }

private:
    base_type base_;
};

// Specialization for void value.
template <typename Handler, typename PriorResult>
class value_handler_invoker<Handler, PriorResult, void> final {
    using base_type = context_adapter<Handler,
                                      void, typename PriorResult::error_type, 0>;

public:
    using result_type = typename base_type::result_type;

    explicit value_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context, PriorResult& result) {
        return base_.template call<>(context);
    }

private:
    base_type base_;
};

// Wraps a handler that may accept a context and error argument.
template <typename Handler, typename PriorResult,
          typename E = typename PriorResult::error_type>
class error_handler_invoker final {
    using base_type = context_adapter<Handler,
                                      typename PriorResult::value_type, void, 1>;
    using error_arg_type =
        typename ::fit::callable_traits<Handler>::args::template at<
            base_type::next_arg_index>;
    static_assert(
        (std::is_same<error_arg_type, E>::value &&
         std::is_copy_constructible<error_arg_type>::value) ||
            std::is_same<error_arg_type, E&>::value ||
            std::is_same<error_arg_type, const E&>::value,
        "The provided handler's last argument was expected to be of type "
        "E&, const E&, or E (if the error is copy-constructible).  "
        "Please refer to the combinator's documentation for a list of "
        "supported handler function signatures.");

public:
    using result_type = typename base_type::result_type;

    explicit error_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context, PriorResult& result) {
        return base_.template call<E&>(context, result.error());
    }

private:
    base_type base_;
};

// Specialization for void error.
template <typename Handler, typename PriorResult>
class error_handler_invoker<Handler, PriorResult, void> final {
    using base_type = context_adapter<Handler,
                                      typename PriorResult::value_type, void, 0>;

public:
    using result_type = typename base_type::result_type;

    explicit error_handler_invoker(Handler handler)
        : base_(std::move(handler)) {}

    result_type operator()(::fit::context& context, PriorResult& result) {
        return base_.template call<>(context);
    }

private:
    base_type base_;
};

// The continuation produced by |fit::promise::then()|.
template <typename PriorPromise, typename ResultHandler>
class then_continuation final {
    using invoker_type = ::fit::internal::result_handler_invoker<
        ResultHandler, typename PriorPromise::result_type>;

public:
    then_continuation(PriorPromise prior_promise, ResultHandler handler)
        : prior_(std::move(prior_promise)),
          invoker_(std::move(handler)) {}

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
    using invoker_type = ::fit::internal::value_handler_invoker<
        ValueHandler, typename PriorPromise::result_type>;

public:
    and_then_continuation(PriorPromise prior_promise, ValueHandler handler)
        : prior_(std::move(prior_promise)),
          invoker_(std::move(handler)) {}

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
    using invoker_type = ::fit::internal::error_handler_invoker<
        ErrorHandler, typename PriorPromise::result_type>;

public:
    or_else_continuation(PriorPromise prior_promise, ErrorHandler handler)
        : prior_(std::move(prior_promise)),
          invoker_(std::move(handler)) {}

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
    using invoker_type = ::fit::internal::result_handler_invoker<
        InspectHandler, typename PriorPromise::result_type>;

public:
    inspect_continuation(PriorPromise prior_promise, InspectHandler handler)
        : prior_(std::move(prior_promise)),
          invoker_(std::move(handler)) {}

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

// Returns true if all arguments are true or if there are none.
inline bool all_true() {
    return true;
}
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

    ::fit::result<std::tuple<typename Promises::result_type...>> operator()(
        ::fit::context& context) {
        return evaluate(context, std::index_sequence_for<Promises...>{});
    }

private:
    template <size_t... i>
    ::fit::result<std::tuple<typename Promises::result_type...>> evaluate(
        ::fit::context& context, std::index_sequence<i...>) {
        bool done = all_true(std::get<i>(promises_)(context)...);
        if (!done)
            return ::fit::pending();
        return ::fit::ok(std::make_tuple(std::get<i>(promises_).take_result()...));
    }

    std::tuple<future_impl<Promises>...> promises_;
};

} // namespace internal

template <typename PromiseHandler>
inline promise_impl<::fit::internal::promise_continuation<PromiseHandler>>
make_promise(PromiseHandler handler);

template <typename Continuation>
inline promise_impl<Continuation> make_promise_with_continuation(
    Continuation continuation);

} // namespace fit

#endif // LIB_FIT_PROMISE_INTERNAL_H_
