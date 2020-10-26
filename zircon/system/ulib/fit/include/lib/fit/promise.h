// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_PROMISE_H_
#define LIB_FIT_PROMISE_H_

#include <assert.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "function.h"
#include "promise_internal.h"
#include "result.h"
#include "variant.h"

namespace fit {

// A |fit::promise| is a building block for asynchronous control flow that
// wraps an asynchronous task in the form of a "continuation" that is
// repeatedly invoked by an executor until it produces a result.
//
// Additional asynchronous tasks can be chained onto the promise using
// a variety of combinators such as |then()|.
//
// Use |fit::make_promise()| to create a promise.
// Use |fit::make_ok_promise()| to create a promise that immediately returns a value.
// Use |fit::make_error_promise()| to create a promise that immediately returns an error.
// Use |fit::make_result_promise()| to create a promise that immediately returns a result.
// Use |fit::future| to more conveniently hold a promise or its result.
// Use |fit::pending_task| to wrap a promise as a pending task for execution.
// Use |fit::executor| to execute a pending task.
// See examples below.
//
// Always look to the future; never look back.
//
// SYNOPSIS
//
// |V| is the type of value produced when the completes successfully.
// Defaults to |void|.
//
// |E| is the type of error produced when the completes with an error.
// Defaults to |void|.
//
// Class members are documented in |fit::promise_impl|.
//
// CHAINING PROMISES USING COMBINATORS
//
// Promises can be chained together using combinators such as |then()|
// which consume the original promise(s) and return a new combined promise.
//
// For example, the |then()| combinator returns a promise that has the effect
// of asynchronously awaiting completion of the prior promise (the instance
// upon which |then()| was called) then delivering its result to a handler
// function.
//
// Available combinators defined in this library:
//
//    |then()|: run a handler when prior promise completes
//    |and_then()|: run a handler when prior promise completes successfully
//    |or_else()|: run a handler when prior promise completes with an error
//    |inspect()|: examine result of prior promise
//    |discard_result()|: discard result and unconditionally return
//                        fit::result<> when prior promise completes
//    |wrap_with()|: applies a wrapper to the promise
//    |box()|: wraps the promise's continuation into a |fit::function|
//    |fit::join_promises()|: await multiple promises in an argument list,
//                            once they all complete return a tuple of
//                            their results
//    |fit::join_promise_vector()|: await multiple promises in a vector,
//                                  once they all complete return a vector
//                                  of their results
//
// You can also create your own custom combinators by crafting new
// types of continuations.
//
// CONTINUATIONS AND HANDLERS
//
// Internally, |fit::promise| wraps a continuation (a kind of callable
// object) that holds the state of the asynchronous task and provides a
// means for making progress through repeated invocation.
//
// A promise's continuation is generated through the use of factories
// such as |make_promise()| and combinators such as |then()|.  Most of
// these functions accept a client-supplied "handler" (another kind
// of callable object, often a lambda expression) which performs the actual
// computations.
//
// Continuations have a very regular interface: they always accept a
// |fit::context&| argument and return a |fit::result|.  Conversely, handlers
// have a very flexible interface: clients can provide them in many forms
// all of which are documented by the individual functions which consume them.
// It's pretty easy to use: the library takes care of wrapping client-supplied
// handlers of all supported forms into the continuations it uses internally.
//
// THEORY OF OPERATION
//
// On its own, a promise is "inert"; it only makes progress in response to
// actions taken by its owner.  The state of the promise never changes
// spontaneously or concurrently.
//
// Typically, a promise is executed by wrapping it into a |fit::pending_task|
// and scheduling it for execution using |fit::executor::schedule_task()|.
// A promise's |operator(fit::context&)| can also be invoked directly by its owner
// from within the scope of another task (this is used to implement combinators
// and futures) though the principle is the same.
//
// |fit::executor| is an abstract class that encapsulates a strategy for
// executing tasks.  The executor is responsible for invoking each tasks's
// continuation until the task returns a non-pending result, indicating that
// the task has been completed.
//
// The method of execution and scheduling of each continuation call is left
// to the discretion of each executor implementation.  Typical executor
// implementations may dispatch tasks on an event-driven message loop or on
// a thread pool.  Developers are responsible for selecting appropriate
// executor implementations for their programs.
//
// During each invocation, the executor passes the continuation an execution
// context object represented by a subclass of |fit::context|.  The continuation
// attempts to make progress then returns a value of type |fit::result| to
// indicate whether it completed successfully (signaled by |fit::ok()|),
// failed with an error (signaled by |fit::error()|, or was unable to complete
// the task during that invocation (signaled by |fit::pending()|).
// For example, a continuation may be unable to complete the task if it must
// asynchronously await completion of an I/O or IPC operation before it
// can proceed any further.
//
// If the continuation was unable to complete the task during its invocation,
// it may to call |fit::context::suspend_task()| to acquire a
// |fit::suspended_task| object.  The continuation then arranges for the
// task to be resumed asynchronously (with |fit::suspended_task::resume_task()|)
// once it becomes possible for the promise to make forward progress again.
// Finally, the continuation returns returns |fit::pending()| to indicate to
// the executor that it was unable to complete the task during that invocation.
//
// When the executor receives a pending result from a task's continuation,
// it moves the task into a table of suspended tasks.  A suspended task
// is considered abandoned if has not been resume and all remaining
// |fit::suspended_task| handles representing it have been dropped.
// When a task is abandoned, the executor removes it from its table of
// suspended tasks and destroys the task because it is not possible for the task
// to be resumed or to make progress from that state.
//
// See also |fit::single_threaded_executor| for a simple executor implementation.
//
// BOXED AND UNBOXED PROMISES
//
// To make combination and execution as efficient as possible, the promises
// returned by |fit::make_promise| and by combinators are parameterized by
// complicated continuation types that are hard to describe, often consisting of
// nested templates and lambdas.  These are referred to as "unboxed"
// promises.  In contrast, "boxed" promises are parameterized by a
// |fit::function| that hides (or "erases") the type of the continuation
// thereby yielding type that is easier to describe.
//
// You can recognize boxed and unboxed promises by their types.
// Here are two examples:
//
// - A boxed promise type: `fit::promise<void, void>` which is an alias for
//  `fit::promise_impl<void, void, std::function<fit::result<void, void>>`.
// - An unboxed promise type: `fit::promise_impl<void, void,
//   fit::internal::then_continuation<...something unintelligible...>>`
//
// Although boxed promises are easier to manipulate, they may cause the
// continuation to be allocated on the heap.  Chaining boxed promises can
// result in multiple allocations being produced.
//
// Conversely, unboxed promises have full type information.  Not only does
// this defer heap allocation but it also makes it easier for the C++
// compiler to fuse a chains of unboxed promises together into a single
// object that is easier to optimize.
//
// Unboxed promises can be boxed by assigning them to a boxed promise
// type (such as |fit::promise<>|) or using the |box()| combinator.
//
// As a rule of thumb, always defer boxing of promises until it is necessary
// to transport them using a simpler type.
//
// Do this: (chaining as a single expression performs at most one heap allocation)
//
//     fit::promise<> f = fit::make_promise([] { ... });
//         .then([](fit::result<>& result) { ... });
//         .and_then([] { ... });
//
// Or this: (still only performs at most one heap allocation)
//
//     auto f = fit::make_promise([] { ... });
//     auto g = f.then([](fit::result<>& result) { ... });
//     auto h = g.and_then([] { ... });
//     fit::promise<> boxed_h = h;
//
// But don't do this: (incurs up to three heap allocations due to eager boxing)
//
//     fit::promise<> f = fit::make_promise([] { ... });
//     fit::promise<> g = f.then([](fit::result<>& result) { ... });
//     fit::promise<> h = g.and_then([] { ... });
//
// SINGLE OWNERSHIP MODEL
//
// Promises have single-ownership semantics.  This means that there
// can only be at most one reference to the task represented by its
// continuation along with any state held by that continuation.
//
// When a combinator is applied to a promise, ownership of its continuation
// is transferred to the combined promise, leaving the original promise
// in an "empty" state without a continuation.  Note that it is an error
// to attempt to invoke an empty promise (will assert at runtime).
//
// This model greatly simplifies reasoning about object lifetime.
// If a promise goes out of scope without completing its task, the task
// is considered "abandoned", causing all associated state to be destroyed.
//
// Note that a promise may capture references to other objects whose lifetime
// differs from that of the promise.  It is the responsibility of the promise
// to ensure reachability of the objects whose reference it captures such
// as by using reference counted pointers, weak pointers, or other appropriate
// mechanisms to ensure memory safety.
//
// THREADING MODEL
//
// Promise objects are not thread-safe themselves.  You cannot call their
// methods concurrently (or re-entrantly).  However, promises can safely
// be moved to other threads and executed there (unless their continuation
// requires thread affinity for some reason but that's beyond the scope
// of this document).
//
// This property of being thread-independent, combined with the single
// ownership model, greatly simplifies the implementation of thread pool
// based executors.
//
// RESULT RETENTION AND FIT::FUTURES
//
// A promise's continuation can only be executed to completion once.
// After it completes, it cannot be run again.
//
// This method of execution is very efficient; the promise's result is returned
// directly to its invoker; it is not copied or retained within the promise
// object itself.  It is entirely the caller's responsibility to decide how to
// consume or retain the result if need be.
//
// For example, the caller can move the promise into a |fit::future| to
// more conveniently hold either the promise or its result upon completion.
//
// CLARIFICATION OF NOMENCLATURE
//
// In this library, the words "promise" and "future" have the following
// definitions:
//
// - A *promise* holds the function that performs an asynchronous task.
//   It is the means to produce a value.
// - A *future* holds the value produced by an asynchronous task or a
//   promise to produce that value if the task has not yet completed.
//   It is a proxy for a value that is to be computed.
//
// Be aware that other libraries may use these terms slightly differently.
//
// For more information about the theory of futures and promises, see
// https://en.wikipedia.org/wiki/Futures_and_promises.
//
// COMPARISON WITH STD::FUTURE
//
// |std::future| provides a mechanism for running asynchronous tasks
// and awaiting their results on other threads.  Waiting can be performed
// either by blocking the waiting thread or by polling the future.
// The manner in which tasks are scheduled and executed is entirely
// controlled by the C++ standard library and offers limited control
// to developers.
//
// |fit::promise| and |fit::future| provide a mechanism for running asynchronous
// tasks, chaining additional tasks using combinators, and awaiting their
// results.  An executor is responsible for suspending tasks awaiting
// results of other tasks and is at liberty to run other tasks on the
// same thread rather than blocking.  In addition, developers can create custom
// executors to implement their own policies for running tasks.
//
// Decoupling awaiting from blocking makes |fit::promise| quite versatile.
// |fit::promise| can also interoperate with other task dispatching mechanisms
// (including |std::future|) using adapters such as |fit::bridge|.
//
// EXAMPLE
//
// -
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/utest/fit/examples/promise_example1.cc
// -
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/utest/fit/examples/promise_example2.cc
//
template <typename V = void, typename E = void>
using promise = promise_impl<function<result<V, E>(fit::context&)>>;

// Promise implementation details.
// See |fit::promise| documentation for more information.
template <typename Continuation>
class promise_impl final {
  static_assert(::fit::internal::is_continuation<Continuation>::value,
                "Continuation type is invalid.  A continuation is a callable object "
                "with this signature: fit::result<V, E>(fit::context&).");

  using state_type = nullable<Continuation>;

 public:
  // The type of callable object held by the promise.
  // Its signature is: result_type(fit::context&).
  using continuation_type = Continuation;

  // The promise's result type.
  // Equivalent to fit::result<value_type, error_type>.
  using result_type = typename ::fit::internal::continuation_traits<Continuation>::result_type;

  // The type of value produced when the promise completes successfully.
  // May be void.
  using value_type = typename result_type::value_type;

  // The type of value produced when the promise completes with an error.
  // May be void.
  using error_type = typename result_type::error_type;

  // Creates an empty promise without a continuation.
  // A continuation must be assigned before the promise can be used.
  promise_impl() = default;
  explicit promise_impl(decltype(nullptr)) {}

  promise_impl(const promise_impl&) = delete;
  promise_impl& operator=(const promise_impl&) = delete;

  // Constructs the promise by taking the continuation from another promise,
  // leaving the other promise empty.
  promise_impl(promise_impl&& other) : state_{std::move(other.state_)} { other.state_.reset(); }

  // Assigns the promise by taking the continuation from another promise,
  // leaving the other promise empty.
  promise_impl& operator=(promise_impl&& other) {
    if (this != &other) {
      state_ = std::move(other.state_);
      other.state_.reset();
    }
    return *this;
  }

  // Creates a promise with a continuation.
  // If |continuation| equals nullptr then the promise is empty.
  explicit promise_impl(continuation_type continuation) : state_(std::move(continuation)) {}

  // Converts from a promise holding a continuation that is assignable to
  // to this promise's continuation type.
  //
  // This is typically used to create a promise with a boxed continuation
  // type (such as |fit::function|) from an unboxed promise produced by
  // |fit::make_promise| or by combinators.
  //
  // EXAMPLE
  //
  //     // f is a promise_impl with a complicated unboxed type
  //     auto f = fit::make_promise([] { ... });
  //
  //     // g wraps f's continuation
  //     fit::promise<> g = std::move(f);
  //
  template <
      typename OtherContinuation,
      std::enable_if_t<!std::is_same<continuation_type, OtherContinuation>::value &&
                           std::is_constructible<continuation_type, OtherContinuation&&>::value,
                       bool> = true>
  promise_impl(promise_impl<OtherContinuation> other)
      : state_(other.state_.has_value() ? state_type(continuation_type(std::move(*other.state_)))
                                        : state_type()) {}

  // Destroys the promise, releasing its continuation.
  ~promise_impl() = default;

  // Returns true if the promise is non-empty (has a valid continuation).
  explicit operator bool() const { return state_.has_value(); }

  // Invokes the promise's continuation.
  //
  // This method should be called by an executor to evaluate the promise.
  // If the result's state is |result_state::pending| then the executor
  // is responsible for arranging to invoke the promise's continuation
  // again once it determines that it is possible to make progress
  // towards completion of the promise encapsulated within the promise.
  //
  // Once the continuation returns a result with status |result_state::ok|
  // or |result_state::error|, the promise is assigned an empty continuation.
  //
  // Asserts that the promise is non-empty.
  result_type operator()(context& context) {
    result_type result = (state_.value())(context);
    if (!result.is_pending())
      state_.reset();
    return result;
  }

  // Takes the promise's continuation, leaving it in an empty state.
  // Asserts that the promise is non-empty.
  continuation_type take_continuation() {
    auto continuation = std::move(state_.value());
    state_.reset();
    return continuation;
  }

  // Discards the promise's continuation, leaving it empty.
  promise_impl& operator=(decltype(nullptr)) {
    state_.reset();
    return *this;
  }

  // Assigns the promise's continuation.
  promise_impl& operator=(continuation_type continuation) {
    state_ = std::move(continuation);
    return *this;
  }

  // Swaps the promises' continuations.
  void swap(promise_impl& other) {
    using std::swap;
    swap(state_, other.state_);
  }

  // Returns an unboxed promise which invokes the specified handler
  // function after this promise completes (successfully or unsuccessfully),
  // passing its result.
  //
  // The received result's state is guaranteed to be either
  // |fit::result_state::ok| or |fit::result_state::error|, never
  // |fit::result_state::pending|.
  //
  // |handler| is a callable object (such as a lambda) which consumes the
  // result of this promise and returns a new result with any value type
  // and error type.  Must not be null.
  //
  // The handler must return one of the following types:
  // - void
  // - fit::result<new_value_type, new_error_type>
  // - fit::ok<new_value_type>
  // - fit::error<new_error_type>
  // - fit::pending
  // - fit::promise<new_value_type, new_error_type>
  // - any callable or unboxed promise with the following signature:
  //   fit::result<new_value_type, new_error_type>(fit::context&)
  //
  // The handler must accept one of the following argument lists:
  // - (result_type&)
  // - (const result_type&)
  // - (fit::context&, result_type&)
  // - (fit::context&, const result_type&)
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     auto f = fit::make_promise(...)
  //         .then([] (fit::result<int, std::string>& result)
  //                   -> fit::result<std::string, void> {
  //             if (result.is_ok()) {
  //                 printf("received value: %d\n", result.value());
  //                 if (result.value() % 15 == 0)
  //                     return ::fit::ok("fizzbuzz");
  //                 if (result.value() % 3 == 0)
  //                     return ::fit::ok("fizz");
  //                 if (result.value() % 5 == 0)
  //                     return ::fit::ok("buzz");
  //                 return ::fit::ok(std::to_string(result.value()));
  //             } else {
  //                 printf("received error: %s\n", result.error().c_str());
  //                 return ::fit::error();
  //             }
  //         })
  //         .then(...);
  //
  template <typename ResultHandler>
  promise_impl<::fit::internal::then_continuation<promise_impl, ResultHandler>> then(
      ResultHandler handler) {
    static_assert(is_callable<ResultHandler>::value, "ResultHandler must be a callable object.");

    assert(!is_null(handler));
    assert(state_.has_value());
    return make_promise_with_continuation(
        ::fit::internal::then_continuation<promise_impl, ResultHandler>(std::move(*this),
                                                                        std::move(handler)));
  }

  // Returns an unboxed promise which invokes the specified handler
  // function after this promise completes successfully, passing its
  // resulting value.
  //
  // |handler| is a callable object (such as a lambda) which consumes the
  // result of this promise and returns a new result with any value type
  // but the same error type.  Must not be null.
  //
  // The handler must return one of the following types:
  // - void
  // - fit::result<new_value_type, error_type>
  // - fit::ok<new_value_type>
  // - fit::error<error_type>
  // - fit::pending
  // - fit::promise<new_value_type, error_type>
  // - any callable or unboxed promise with the following signature:
  //   fit::result<new_value_type, error_type>(fit::context&)
  //
  // The handler must accept one of the following argument lists:
  // - (value_type&)
  // - (const value_type&)
  // - (fit::context&, value_type&)
  // - (fit::context&, const value_type&)
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     auto f = fit::make_promise(...)
  //         .and_then([] (const int& value) {
  //             printf("received value: %d\n", value);
  //             if (value % 15 == 0)
  //                 return ::fit::ok("fizzbuzz");
  //             if (value % 3 == 0)
  //                 return ::fit::ok("fizz");
  //             if (value % 5 == 0)
  //                 return ::fit::ok("buzz");
  //             return ::fit::ok(std::to_string(value));
  //         })
  //         .then(...);
  //
  template <typename ValueHandler>
  promise_impl<::fit::internal::and_then_continuation<promise_impl, ValueHandler>> and_then(
      ValueHandler handler) {
    static_assert(is_callable<ValueHandler>::value, "ValueHandler must be a callable object.");

    assert(!is_null(handler));
    assert(state_.has_value());
    return make_promise_with_continuation(
        ::fit::internal::and_then_continuation<promise_impl, ValueHandler>(std::move(*this),
                                                                           std::move(handler)));
  }

  // Returns an unboxed promise which invokes the specified handler
  // function after this promise completes with an error, passing its
  // resulting error.
  //
  // |handler| is a callable object (such as a lambda) which consumes the
  // result of this promise and returns a new result with any error type
  // but the same value type.  Must not be null.
  //
  // The handler must return one of the following types:
  // - void
  // - fit::result<value_type, new_error_type>
  // - fit::ok<value_type>
  // - fit::error<new_error_type>
  // - fit::pending
  // - fit::promise<value_type, new_error_type>
  // - any callable or unboxed promise with the following signature:
  //   fit::result<value_type, new_error_type>(fit::context&)
  //
  // The handler must accept one of the following argument lists:
  // - (error_type&)
  // - (const error_type&)
  // - (fit::context&, error_type&)
  // - (fit::context&, const error_type&)
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     auto f = fit::make_promise(...)
  //         .or_else([] (const std::string& error) {
  //             printf("received error: %s\n", error.c_str());
  //             return ::fit::error();
  //         })
  //         .then(...);
  //
  template <typename ErrorHandler>
  promise_impl<::fit::internal::or_else_continuation<promise_impl, ErrorHandler>> or_else(
      ErrorHandler handler) {
    static_assert(is_callable<ErrorHandler>::value, "ErrorHandler must be a callable object.");

    assert(!is_null(handler));
    assert(state_.has_value());
    return make_promise_with_continuation(
        ::fit::internal::or_else_continuation<promise_impl, ErrorHandler>(std::move(*this),
                                                                          std::move(handler)));
  }

  // Returns an unboxed promise which invokes the specified handler
  // function after this promise completes (successfully or unsuccessfully),
  // passing it the promise's result then delivering the result onwards
  // to the next promise once the handler returns.
  //
  // The handler receives a const reference, or non-const reference
  // depending on the signature of the handler's last argument.
  //
  // - Const references are especially useful for inspecting a
  //   result mid-stream without modification, such as printing it for
  //   debugging.
  // - Non-const references are especially useful for synchronously
  //   modifying a result mid-stream, such as clamping its bounds or
  //   injecting a default value.
  //
  // |handler| is a callable object (such as a lambda) which can examine
  // or modify the incoming result.  Unlike |then()|, the handler does
  // not need to propagate the result onwards.  Must not be null.
  //
  // The handler must return one of the following types:
  // - void
  //
  // The handler must accept one of the following argument lists:
  // - (result_type&)
  // - (const result_type&)
  // - (fit::context&, result_type&)
  // - (fit::context&, const result_type&)
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     auto f = fit::make_promise(...)
  //         .inspect([] (const fit::result<int, std::string>& result) {
  //             if (result.is_ok())
  //                 printf("received value: %d\n", result.value());
  //             else
  //                 printf("received error: %s\n", result.error().c_str());
  //         })
  //         .then(...);
  //
  template <typename InspectHandler>
  promise_impl<::fit::internal::inspect_continuation<promise_impl, InspectHandler>> inspect(
      InspectHandler handler) {
    static_assert(is_callable<InspectHandler>::value, "InspectHandler must be a callable object.");
    static_assert(std::is_void<typename callable_traits<InspectHandler>::return_type>::value,
                  "InspectHandler must return void.");

    assert(!is_null(handler));
    assert(state_.has_value());
    return make_promise_with_continuation(
        ::fit::internal::inspect_continuation<promise_impl, InspectHandler>(std::move(*this),
                                                                            std::move(handler)));
  }

  // Returns an unboxed promise which discards the result of this promise
  // once it completes, thereby always producing a successful result of
  // type fit::result<void, void> regardless of whether this promise
  // succeeded or failed.
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     auto f = fit::make_promise(...)
  //         .discard_result()
  //         .then(...);
  //
  promise_impl<::fit::internal::discard_result_continuation<promise_impl>> discard_result() {
    assert(state_.has_value());
    return make_promise_with_continuation(
        ::fit::internal::discard_result_continuation<promise_impl>(std::move(*this)));
  }

  // Applies a |wrapper| to the promise.  Invokes the wrapper's |wrap()|
  // method, passes the promise to the wrapper by value followed by any
  // additional |args| passed to |wrap_with()|, then returns the wrapper's
  // result.
  //
  // |Wrapper| is a type that implements a method called |wrap()| which
  // accepts a promise as its argument and produces a wrapped result of
  // any type, such as another promise.
  //
  // Asserts that the promise is non-empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  // In this example, |fit::sequencer| is a wrapper type that imposes
  // FIFO execution order onto a sequence of wrapped promises.
  //
  //     // This wrapper type is intended to be applied to
  //     // a sequence of promises so we store it in a variable.
  //     fit::sequencer seq;
  //
  //     // This task consists of some amount of work that must be
  //     // completed sequentially followed by other work that can
  //     // happen in any order.  We use |wrap_with()| to wrap the
  //     // sequential work with the sequencer.
  //     fit::promise<> perform_complex_task() {
  //         return fit::make_promise([] { /* do sequential work */ })
  //             .then([] (fit::result<> result) { /* this will also be wrapped */ })
  //             .wrap_with(seq)
  //             .then([] (fit::result<> result) { /* do more work */ });
  //     }
  //
  // This example can also be written without using |wrap_with()|.
  // The behavior is equivalent but the syntax may seem more awkward.
  //
  //     fit::sequencer seq;
  //
  //     promise<> perform_complex_task() {
  //         return seq.wrap(
  //                 fit::make_promise([] { /* sequential work */ })
  //             ).then([] (fit::result<> result) { /* more work */ });
  //     }
  //
  template <typename Wrapper, typename... Args>
  decltype(auto) wrap_with(Wrapper& wrapper, Args... args) {
    assert(state_.has_value());
    return wrapper.wrap(std::move(*this), std::forward<Args>(args)...);
  }

  // Wraps the promise's continuation into a |fit::function|.
  //
  // A boxed promise is easier to store and pass around than the unboxed
  // promises produced by |fit::make_promise()| and combinators, though boxing
  // may incur a heap allocation.
  //
  // It is a good idea to defer boxing the promise until after all
  // desired combinators have been applied to prevent unnecessary heap
  // allocation during intermediate states of the promise's construction.
  //
  // Returns an empty promise if this promise is empty.
  // This method consumes the promise's continuation, leaving it empty.
  //
  // EXAMPLE
  //
  //     // f's is a fit::promise_impl<> whose continuation contains an
  //     // anonymous type (the lambda)
  //     auto f = fit::make_promise([] {});
  //
  //     // g's type will be fit::promise<> due to boxing
  //     auto boxed_f = f.box();
  //
  //     // alternately, we can get exactly the same effect by assigning
  //     // the unboxed promise to a variable of a named type instead of
  //     // calling box()
  //     fit::promise<> boxed_f = std::move(f);
  //
  promise_impl<function<result_type(context&)>> box() { return std::move(*this); }

 private:
  template <typename>
  friend class promise_impl;

  state_type state_;
};

template <typename Continuation>
void swap(promise_impl<Continuation>& a, promise_impl<Continuation>& b) {
  a.swap(b);
}

template <typename Continuation>
bool operator==(const promise_impl<Continuation>& f, decltype(nullptr)) {
  return !f;
}
template <typename Continuation>
bool operator==(decltype(nullptr), const promise_impl<Continuation>& f) {
  return !f;
}
template <typename Continuation>
bool operator!=(const promise_impl<Continuation>& f, decltype(nullptr)) {
  return !!f;
}
template <typename Continuation>
bool operator!=(decltype(nullptr), const promise_impl<Continuation>& f) {
  return !!f;
}

// Makes a promise containing the specified continuation.
//
// This function is used for making a promises given a callable object
// that represents a valid continuation type.  In contrast,
// |fit::make_promise()| supports a wider range of types and should be
// preferred in most situations.
//
// |Continuation| is a callable object with the signature
// fit::result<V, E>(fit::context&).
template <typename Continuation>
inline promise_impl<Continuation> make_promise_with_continuation(Continuation continuation) {
  return promise_impl<Continuation>(std::move(continuation));
}

// Returns an unboxed promise that wraps the specified handler.
// The type of the promise's result is inferred from the handler's result.
//
// |handler| is a callable object (such as a lambda.  Must not be null.
//
// The handler must return one of the following types:
// - void
// - fit::result<value_type, error_type>
// - fit::ok<value_type>
// - fit::error<error_type>
// - fit::pending
// - fit::promise<value_type, error_type>
// - any callable or unboxed promise with the following signature:
//   fit::result<value_type, error_type>(fit::context&)
//
// The handler must accept one of the following argument lists:
// - ()
// - (fit::context&)
//
// See documentation of |fit::promise| for more information.
//
// SYNOPSIS
//
// |Handler| is the handler function type.  It is typically inferred by the
// compiler from the |handler| argument.
//
// EXAMPLE
//
//     enum class weather_type { sunny, glorious, cloudy, eerie, ... };
//
//     weather_type look_outside() { ... }
//     void wait_for_tomorrow(fit::suspended_task task) {
//         ... arrange to call task.resume_task() tomorrow ...
//     }
//
//     fit::promise<weather_type, std::string> wait_for_good_weather(int max_days) {
//         return fit::make_promise([days_left = max_days] (fit::context& context) mutable
//                             -> fit::result<int, std::string> {
//             weather_type weather = look_outside();
//             if (weather == weather_type::sunny || weather == weather_type::glorious)
//                 return fit::ok(weather);
//             if (days_left > 0) {
//                 wait_for_tomorrow(context.suspend_task());
//                 return fit::pending();
//             }
//             days_left--;
//             return fit::error("nothing but grey skies");
//         });
//     }
//
//     auto f = wait_for_good_weather(7)
//         .and_then([] (const weather_type& weather) { ... })
//         .or_else([] (const std::string& error) { ... });
//
template <typename PromiseHandler>
inline promise_impl<::fit::internal::context_handler_invoker<PromiseHandler>> make_promise(
    PromiseHandler handler) {
  static_assert(is_callable<PromiseHandler>::value, "PromiseHandler must be a callable object.");

  assert(!is_null(handler));
  return make_promise_with_continuation(
      ::fit::internal::promise_continuation<PromiseHandler>(std::move(handler)));
}

// Returns an unboxed promise that immediately returns the specified result when invoked.
//
// This function is especially useful for returning promises from functions
// that have multiple branches some of which complete synchronously.
//
// |result| is the result for the promise to return.
//
// See documentation of |fit::promise| for more information.
template <typename V = void, typename E = void>
inline promise_impl<::fit::internal::result_continuation<V, E>> make_result_promise(
    fit::result<V, E> result) {
  return make_promise_with_continuation(
      ::fit::internal::result_continuation<V, E>(std::move(result)));
}
template <typename V = void, typename E = void>
inline promise_impl<::fit::internal::result_continuation<V, E>> make_result_promise(
    fit::ok_result<V> result) {
  return make_promise_with_continuation(
      ::fit::internal::result_continuation<V, E>(std::move(result)));
}
template <typename V = void, typename E = void>
inline promise_impl<::fit::internal::result_continuation<V, E>> make_result_promise(
    fit::error_result<E> result) {
  return make_promise_with_continuation(
      ::fit::internal::result_continuation<V, E>(std::move(result)));
}
template <typename V = void, typename E = void>
inline promise_impl<::fit::internal::result_continuation<V, E>> make_result_promise(
    fit::pending_result result) {
  return make_promise_with_continuation(
      ::fit::internal::result_continuation<V, E>(std::move(result)));
}

// Returns an unboxed promise that immediately returns the specified value when invoked.
//
// This function is especially useful for returning promises from functions
// that have multiple branches some of which complete synchronously.
//
// |value| is the value for the promise to return.
//
// See documentation of |fit::promise| for more information.
template <typename V>
inline promise_impl<::fit::internal::result_continuation<V, void>> make_ok_promise(V value) {
  return make_result_promise(fit::ok(std::move(value)));
}

// Overload of |make_ok_promise()| used when the value type is void.
inline promise_impl<::fit::internal::result_continuation<void, void>> make_ok_promise() {
  return make_result_promise(fit::ok());
}

// Returns an unboxed promise that immediately returns the specified error when invoked.
//
// This function is especially useful for returning promises from functions
// that have multiple branches some of which complete synchronously.
//
// |error| is the error for the promise to return.
//
// See documentation of |fit::promise| for more information.
template <typename E>
inline promise_impl<::fit::internal::result_continuation<void, E>> make_error_promise(E error) {
  return make_result_promise(fit::error(std::move(error)));
}

// Overload of |make_error_promise()| used when the error type is void.
inline promise_impl<::fit::internal::result_continuation<void, void>> make_error_promise() {
  return make_result_promise(fit::error());
}

// Jointly evaluates zero or more promises.
// Returns a promise that produces a std::tuple<> containing the result
// of each promise once they all complete.
//
// EXAMPLE
//
//     auto get_random_number() {
//         return fit::make_promise([] { return rand() % 10 });
//     }
//
//     auto get_random_product() {
//         auto f = get_random_number();
//         auto g = get_random_number();
//         return fit::join_promises(std::move(f), std::move(g))
//             .and_then([] (std::tuple<fit::result<int>, fit::result<int>>& results) {
//                 return fit::ok(results.get<0>.value() + results.get<1>.value());
//             });
//     }
//
template <typename... Promises>
inline promise_impl<::fit::internal::join_continuation<Promises...>> join_promises(
    Promises... promises) {
  return make_promise_with_continuation(
      ::fit::internal::join_continuation<Promises...>(std::move(promises)...));
}

// Jointly evaluates zero or more homogenous promises (same result and error
// type).  Returns a promise that produces a std::vector<> containing the
// result of each promise once they all complete.
//
// EXAMPLE
//
//     auto get_random_number() {
//         return fit::make_promise([] { return rand() % 10 });
//     }
//
//     auto get_random_product() {
//         std::vector<fit::promise<int>> promises;
//         promises.push_back(get_random_number());
//         promises.push_back(get_random_number());
//         return fit::join_promise_vector(std::move(promises))
//             .and_then([] (std::vector<fit::result<int>>& results) {
//                 return fit::ok(results[0].value() + results[1].value());
//             });
//     }
//
template <typename V, typename E>
inline promise_impl<::fit::internal::join_vector_continuation<fit::promise<V, E>>>
join_promise_vector(std::vector<fit::promise<V, E>> promises) {
  return make_promise_with_continuation(
      ::fit::internal::join_vector_continuation<fit::promise<V, E>>(std::move(promises)));
}

// Describes the status of a future.
enum class future_state {
  // The future neither holds a result nor a promise that could produce a result.
  // An empty future cannot make progress until a promise or result is assigned to it.
  empty,
  // The future holds a promise that may eventually produce a result but
  // it currently doesn't have a result.  The future's promise must be
  // invoked in order to make progress from this state.
  pending,
  // The future holds a successful result.
  ok,
  // The future holds an error result.
  error
};

// A |fit::future| holds onto a |fit::promise| until it has completed then
// provides access to its |fit::result|.
//
// SYNOPSIS
//
// |V| is the type of value produced when the completes successfully.
// Defaults to |void|.
//
// |E| is the type of error produced when the completes with an error.
// Defaults to |void|.
//
// THEORY OF OPERATION
//
// A future has a single owner who is responsible for setting its promise
// or result and driving its execution.  Unlike |fit::promise|, a future retains
// the result produced by completion of its asynchronous task.  Result retention
// eases the implementation of combined tasks that need to await the results
// of other tasks before proceeding.
//
// See the example for details.
//
// A future can be in one of four states, depending on whether it holds...
// - a successful result: |fit::future_state::ok|
// - an error result: |fit::future_state::error|
// - a promise that may eventually produce a result: |fit::future_state::pending|
// - neither: |fit::future_state_empty|
//
// On its own, a future is "inert"; it only makes progress in response to
// actions taken by its owner.  The state of the future never changes
// spontaneously or concurrently.
//
// When the future's state is |fit::future_state::empty|, its owner is
// responsible for setting the future's promise or result thereby moving the
// future into the pending or ready state.
//
// When the future's state is |fit::future_state::pending|, its owner is
// responsible for calling the future's |operator()| to invoke the promise.
// If the promise completes and returns a result, the future will transition
// to the ok or error state according to the result.  The promise itself will
// then be destroyed since it has fulfilled its purpose.
//
// When the future's state is |fit::future_state::ok|, its owner is responsible
// for consuming the stored value using |value()|, |take_value()|,
// |result()|, |take_result()|, or |take_ok_result()|.
//
// When the future's state is |fit::future_state::error|, its owner is
// responsible for consuming the stored error using |error()|, |take_error()|,
// |result()|, |take_result()|, or |take_error_result()|.
//
// See also |fit::promise| for more information about promises and their
// execution.
//
// EXAMPLE
//
// -
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/utest/fit/examples/promise_example2.cc
template <typename V = void, typename E = void>
using future = future_impl<promise<V, E>>;

// Future implementation details.
// See |fit::future| documentation for more information.
template <typename Promise>
class future_impl final {
 public:
  // The type of promise held by the future.
  using promise_type = Promise;

  // The promise's result type.
  // Equivalent to fit::result<value_type, error_type>.
  using result_type = typename Promise::result_type;

  // The type of value produced when the promise completes successfully.
  // May be void.
  using value_type = typename Promise::value_type;

  // The type of value produced when the promise completes with an error.
  // May be void.
  using error_type = typename Promise::error_type;

  // Creates a future in the empty state.
  future_impl() = default;
  future_impl(decltype(nullptr)) {}

  // Creates a future and assigns a promise to compute its result.
  // If the promise is empty, the future enters the empty state.
  // Otherwise the future enters the pending state.
  explicit future_impl(promise_type promise) {
    if (promise) {
      state_.template emplace<1>(std::move(promise));
    }
  }

  // Creates a future and assigns its result.
  // If the result is pending, the future enters the empty state.
  // Otherwise the future enters the ok or error state.
  explicit future_impl(result_type result) {
    if (result) {
      state_.template emplace<2>(std::move(result));
    }
  }

  // Moves from another future, leaving the other one in an empty state.
  future_impl(future_impl&& other) : state_(std::move(other.state_)) {
    other.state_.template emplace<0>();
  }

  // Destroys the promise, releasing its promise and result (if any).
  ~future_impl() = default;

  // Returns the state of the future: empty, pending, ok, or error.
  future_state state() const {
    switch (state_.index()) {
      case 0:
        return future_state::empty;
      case 1:
        return future_state::pending;
      case 2:
        return state_.template get<2>().is_ok() ? future_state::ok : future_state::error;
    }
    __builtin_unreachable();
  }

  // Returns true if the future's state is not |fit::future_state::empty|:
  // it either holds a result or holds a promise that can be invoked to make
  // progress towards obtaining a result.
  explicit operator bool() const { return !is_empty(); }

  // Returns true if the future's state is |fit::future_state::empty|:
  // it does not hold a result or a promise so it cannot make progress.
  bool is_empty() const { return state() == fit::future_state::empty; }

  // Returns true if the future's state is |fit::future_state::pending|:
  // it does not hold a result yet but it does hold a promise that can be invoked
  // to make progress towards obtaining a result.
  bool is_pending() const { return state() == fit::future_state::pending; }

  // Returns true if the future's state is |fit::future_state::ok|:
  // it holds a value that can be retrieved using |value()|, |take_value()|,
  // |result()|, |take_result()|, or |take_ok_result()|.
  bool is_ok() const { return state() == fit::future_state::ok; }

  // Returns true if the future's state is |fit::future_state::error|:
  // it holds an error that can be retrieved using |error()|, |take_error()|,
  // |result()|, |take_result()|, or |take_error_result()|.
  bool is_error() const { return state() == fit::future_state::error; }

  // Returns true if the future's state is either |fit::future_state::ok| or
  // |fit::future_state::error|.
  bool is_ready() const { return state_.index() == 2; }

  // Evaluates the future and returns true if its result is ready.
  // Asserts that the future is not empty.
  //
  // If the promise completes and returns a result, the future will transition
  // to the ok or error state according to the result.  The promise itself will
  // then be destroyed since it has fulfilled its purpose.
  bool operator()(fit::context& context) {
    switch (state_.index()) {
      case 0:
        return false;
      case 1: {
        result_type result = state_.template get<1>()(context);
        if (!result)
          return false;
        state_.template emplace<2>(std::move(result));
        return true;
      }
      case 2:
        return true;
    }
    __builtin_unreachable();
  }

  // Gets a reference to the future's promise.
  // Asserts that the future's state is |fit::future_state::pending|.
  const promise_type& promise() const {
    assert(is_pending());
    return state_.template get<1>();
  }

  // Takes the future's promise, leaving it in an empty state.
  // Asserts that the future's state is |fit::future_state::pending|.
  promise_type take_promise() {
    assert(is_pending());
    auto promise = std::move(state_.template get<1>());
    state_.template emplace<0>();
    return promise;
  }

  // Gets a reference to the future's result.
  // Asserts that the future's state is |fit::future_state::ok| or
  // |fit::future_state::error|.
  result_type& result() {
    assert(is_ready());
    return state_.template get<2>();
  }
  const result_type& result() const {
    assert(is_ready());
    return state_.template get<2>();
  }

  // Takes the future's result, leaving it in an empty state.
  // Asserts that the future's state is |fit::future_state::ok| or
  // |fit::future_state::error|.
  result_type take_result() {
    assert(is_ready());
    auto result = std::move(state_.template get<2>());
    state_.template emplace<0>();
    return result;
  }

  // Gets a reference to the future's value.
  // Asserts that the future's state is |fit::future_state::ok|.
  template <typename R = value_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  R& value() {
    assert(is_ok());
    return state_.template get<2>().value();
  }
  template <typename R = value_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  const R& value() const {
    assert(is_ok());
    return state_.template get<2>().value();
  }

  // Takes the future's value, leaving it in an empty state.
  // Asserts that the future's state is |fit::future_state::ok|.
  template <typename R = value_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  R take_value() {
    assert(is_ok());
    auto value = state_.template get<2>().take_value();
    state_.template emplace<0>();
    return value;
  }
  ok_result<value_type> take_ok_result() {
    assert(is_ok());
    auto result = state_.template get<2>().take_ok_result();
    state_.template emplace<0>();
    return result;
  }

  // Gets a reference to the future's error.
  // Asserts that the future's state is |fit::future_state::error|.
  template <typename R = error_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  R& error() {
    assert(is_error());
    return state_.template get<2>().error();
  }
  template <typename R = error_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  const R& error() const {
    assert(is_error());
    return state_.template get<2>().error();
  }

  // Takes the future's error, leaving it in an empty state.
  // Asserts that the future's state is |fit::future_state::error|.
  template <typename R = error_type, typename = std::enable_if_t<!std::is_void<R>::value>>
  R take_error() {
    assert(is_error());
    auto error = state_.template get<2>().take_error();
    state_.template emplace<0>();
    return error;
  }
  error_result<error_type> take_error_result() {
    assert(is_error());
    auto result = state_.template get<2>().take_error_result();
    state_.template emplace<0>();
    return result;
  }

  // Move assigns from another future, leaving the other one in an empty state.
  future_impl& operator=(future_impl&& other) = default;

  // Discards the future's promise and result, leaving it empty.
  future_impl& operator=(decltype(nullptr)) {
    state_.template emplace<0>();
    return *this;
  }

  // Assigns a promise to compute the future's result.
  // If the promise is empty, the future enters the empty state.
  // Otherwise the future enters the pending state.
  future_impl& operator=(promise_type promise) {
    if (promise) {
      state_.template emplace<1>(std::move(promise));
    } else {
      state_.template emplace<0>();
    }
    return *this;
  }

  // Assigns the future's result.
  // If the result is pending, the future enters the empty state.
  // Otherwise the future enters the ok or error state.
  future_impl& operator=(result_type result) {
    if (result) {
      state_.template emplace<2>(std::move(result));
    } else {
      state_.template emplace<0>();
    }
    return *this;
  }

  // Swaps the futures' contents.
  void swap(future_impl& other) {
    using std::swap;
    swap(state_, other.state_);
  }

  future_impl(const future_impl&) = delete;
  future_impl& operator=(const future_impl&) = delete;

 private:
  variant<monostate, promise_type, result_type> state_;
};

template <typename Promise>
void swap(future_impl<Promise>& a, future_impl<Promise>& b) {
  a.swap(b);
}

template <typename Promise>
bool operator==(const future_impl<Promise>& f, decltype(nullptr)) {
  return !f;
}
template <typename Promise>
bool operator==(decltype(nullptr), const future_impl<Promise>& f) {
  return !f;
}
template <typename Promise>
bool operator!=(const future_impl<Promise>& f, decltype(nullptr)) {
  return !!f;
}
template <typename Promise>
bool operator!=(decltype(nullptr), const future_impl<Promise>& f) {
  return !!f;
}

// Makes a future containing the specified promise.
template <typename Promise>
future_impl<Promise> make_future(Promise promise) {
  return future_impl<Promise>(std::move(promise));
}

// A pending task holds a |fit::promise| that can be scheduled to run on
// a |fit::executor| using |fit::executor::schedule_task()|.
//
// An executor repeatedly invokes a pending task until it returns true,
// indicating completion.  Note that the promise's resulting value or error
// is discarded since it is not meaningful to the executor.  If you need
// to consume the result, use a combinator such as |fit::pending::then()|
// to capture it prior to wrapping the promise into a pending task.
//
// See documentation of |fit::promise| for more information.
class pending_task final {
 public:
  // The type of promise held by this task.
  using promise_type = promise<void, void>;

  // Creates an empty pending task without a promise.
  pending_task() = default;

  // Creates a pending task that wraps an already boxed promise that returns
  // |fit::result<void, void>|.
  pending_task(promise_type promise) : promise_(std::move(promise)) {}

  // Creates a pending task that wraps any kind of promise, boxed or unboxed,
  // regardless of its result type and with any context that is assignable
  // from this task's context type.
  template <typename Continuation>
  pending_task(promise_impl<Continuation> promise)
      : promise_(promise ? promise.discard_result().box() : promise_type()) {}

  pending_task(pending_task&&) = default;
  pending_task& operator=(pending_task&&) = default;

  // Destroys the pending task, releasing its promise.
  ~pending_task() = default;

  // Returns true if the pending task is non-empty (has a valid promise).
  explicit operator bool() const { return !!promise_; }

  // Evaluates the pending task.
  // If the task completes (returns a non-pending result), the task reverts
  // to an empty state (because the promise it holds has reverted to an empty
  // state) and returns true.
  // It is an error to invoke this method if the pending task is empty.
  bool operator()(fit::context& context) { return !promise_(context).is_pending(); }

  // Extracts the pending task's promise.
  promise_type take_promise() { return std::move(promise_); }

  pending_task(const pending_task&) = delete;
  pending_task& operator=(const pending_task&) = delete;

 private:
  promise_type promise_;
};

// Execution context for an asynchronous task, such as a |fit::promise|,
// |fit::future|, or |fit::pending_task|.
//
// When a |fit::executor| executes a task, it provides the task with an
// execution context which enables the task to communicate with the
// executor and manage its own lifecycle.  Specialized executors may subclass
// |fit::context| and offer additional methods beyond those which are
// defined here, such as to provide access to platform-specific features
// supported by the executor.
//
// The context provided to a task is only valid within the scope of a single
// invocation; the task must not retain a reference to the context across
// invocations.
//
// See documentation of |fit::promise| for more information.
class context {
 public:
  // Gets the executor that is running the task, never null.
  virtual class executor* executor() const = 0;

  // Obtains a handle that can be used to resume the task after it has been
  // suspended.
  //
  // Clients should call this method before returning |fit::pending()| from
  // the task.  See documentation on |fit::executor|.
  virtual suspended_task suspend_task() = 0;

  // Converts this context to a derived context type.
  template <typename Context, typename = std::enable_if_t<std::is_base_of<context, Context>::value>>
  Context& as() & {
    // TODO(fxbug.dev/4060): We should perform a run-time type check here rather
    // than blindly casting.  That's why this method exists.
    return static_cast<Context&>(*this);
  }

 protected:
  virtual ~context() = default;
};

// An abstract interface for executing asynchronous tasks, such as promises,
// represented by |fit::pending_task|.
//
// EXECUTING TASKS
//
// An executor evaluates its tasks incrementally.  During each iteration
// of the executor's main loop, it invokes the next task from its ready queue.
//
// If the task returns true, then the task is deemed to have completed.
// The executor removes the tasks from its queue and destroys it since there
// it nothing left to do.
//
// If the task returns false, then the task is deemed to have voluntarily
// suspended itself pending some event that it is awaiting.  Prior to
// returning, the task should acquire at least one |fit::suspended_task|
// handle from its execution context using |fit::context::suspend_task()|
// to provide a means for the task to be resumed once it can make forward
// progress again.
//
// Once the suspended task is resumed with |fit::suspended_task::resume()|, it
// is moved back to the ready queue and it will be invoked again during a later
// iteration of the executor's loop.
//
// If all |fit::suspended_task| handles for a given task are destroyed without
// the task ever being resumed then the task is also destroyed since there
// would be no way for the task to be resumed from suspension.  We say that
// such a task has been "abandoned".
//
// The executor retains single-ownership of all active and suspended tasks.
// When the executor is destroyed, all of its remaining tasks are also
// destroyed.
//
// Please read |fit::promise| for a more detailed explanation of the
// responsibilities of tasks and executors.
//
// NOTES FOR IMPLEMENTORS
//
// This interface is designed to support a variety of different executor
// implementations.  For example, one implementation might run its tasks on
// a single thread whereas another might dispatch them on an event-driven
// message loop or use a thread pool.
//
// See also |fit::single_threaded_executor| for a concrete implementation.
class executor {
 public:
  // Destroys the executor along with all of its remaining scheduled tasks
  // that have yet to complete.
  virtual ~executor() = default;

  // Schedules a task for eventual execution by the executor.
  //
  // This method is thread-safe.
  virtual void schedule_task(pending_task task) = 0;
};

// Represents a task that is awaiting resumption.
//
// This object has RAII semantics.  If the task is not resumed by at least
// one holder of its |suspended_task| handles, then it will be destroyed
// by the executor since it is no longer possible for the task to make
// progress.  The task is said have been "abandoned".
//
// See documentation of |fit::executor| for more information.
class suspended_task final {
 public:
  // A handle that grants the capability to resume a suspended task.
  // Each issued ticket must be individually resolved.
  using ticket = uint64_t;

  // The resolver mechanism implements a lightweight form of reference
  // counting for tasks that have been suspended.
  //
  // When a suspended task is created in a non-empty state, it receives
  // a pointer to a resolver interface and a ticket.  The ticket is
  // a one-time-use handle that represents the task that was suspended
  // and provides a means to resume it.  The |suspended_task| class ensures
  // that every ticket is precisely accounted for.
  //
  // When |suspended_task::resume_task()| is called on an instance with
  // a valid ticket, the resolver's |resolve_ticket()| method is invoked
  // passing the ticket's value along with *true* to resume the task.  This
  // operation consumes the ticket so the |suspended_task| transitions to
  // an empty state.  The ticket and resolver cannot be used again by
  // this |suspended_task| instance.
  //
  // Similarly, when |suspended_task::reset()| is called on an instance with
  // a valid ticket or when the task goes out of scope on such an instance,
  // the resolver's |resolve_ticket()| method is invoked but this time passes
  // *false* to not resume the task.  As before, the ticket is consumed.
  //
  // Finally, when the |suspended_task| is copied, its ticket is duplicated
  // using |duplicate_ticket()| resulting in two tickets, both of which
  // must be individually resolved.
  //
  // Resuming a task that has already been resumed has no effect.
  // Conversely, a task is considered "abandoned" if all of its tickets
  // have been resolved without it ever being resumed.  See documentation
  // of |fit::promise| for more information.
  //
  // The methods of this class are safe to call from any thread, including
  // threads that may not be managed by the task's executor.
  class resolver {
   public:
    // Duplicates the provided ticket, returning a new ticket.
    // Note: The new ticket may have the same numeric value as the
    //       original ticket but should be considered a distinct instance
    //       that must be separately resolved.
    virtual ticket duplicate_ticket(ticket ticket) = 0;

    // Consumes the provided ticket, optionally resuming its associated task.
    // The provided ticket must not be used again.
    virtual void resolve_ticket(ticket ticket, bool resume_task) = 0;

   protected:
    virtual ~resolver() = default;
  };

  suspended_task() : resolver_(nullptr), ticket_(0) {}

  suspended_task(resolver* resolver, ticket ticket) : resolver_(resolver), ticket_(ticket) {}

  suspended_task(const suspended_task& other);
  suspended_task(suspended_task&& other);

  // Releases the task without resumption.
  //
  // Does nothing if this object does not hold a ticket.
  ~suspended_task();

  // Returns true if this object holds a ticket for a suspended task.
  explicit operator bool() const { return resolver_ != nullptr; }

  // Asks the task's executor to resume execution of the suspended task
  // if it has not already been resumed or completed.  Also releases
  // the task's ticket as a side-effect.
  //
  // Clients should call this method when it is possible for the task to
  // make progress; for example, because some event the task was
  // awaiting has occurred.  See documentation on |fit::executor|.
  //
  // Does nothing if this object does not hold a ticket.
  void resume_task() { resolve(true); }

  // Releases the suspended task without resumption.
  //
  // Does nothing if this object does not hold a ticket.
  void reset() { resolve(false); }

  // Swaps suspended tasks.
  void swap(suspended_task& other);

  suspended_task& operator=(const suspended_task& other);
  suspended_task& operator=(suspended_task&& other);

 private:
  void resolve(bool resume_task);

  resolver* resolver_;
  ticket ticket_;
};

inline void swap(suspended_task& a, suspended_task& b) { a.swap(b); }

}  // namespace fit

#endif  // LIB_FIT_PROMISE_H_
