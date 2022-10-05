// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FUTURE_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FUTURE_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/internal/future.h>
#include <lib/fit/internal/result.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/type_traits.h>
#include <lib/stdcompat/utility.h>

#include <compare>
#include <future>
#include <iostream>
#include <iterator>
#include <new>
#include <optional>
#include <sstream>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace fasync {

// An |fasync::future| is a building block for asynchronous control flow that wraps an asynchronous
// task in the form of a "continuation" that is repeatedly invoked by an executor until it produces
// a result.
//
// Additional asynchronous tasks can be chained onto the future using a variety of combinators such
// as |fasync::then()|.
//
// Use |fasync::make_future()| to create a future.
// Use |fasync::make_value_future()| to create a future that immediately returns a value.
// Use |fasync::make_ok_future()| to create a future that immediately returns a success value.
// Use |fasync::make_error_future()| to create a future that immediately returns an error.
// Use |fasync::make_try_future()| to create a future that immediately returns a result.
// Use |fasync::pending_task| to wrap a future as a pending task for execution.
// Use |fasync::executor| to execute a pending task.
// See examples below.
//
// Always look to the future; never look back.
//
// |FASYNC::FUTURE<T>| SYNOPSIS
//
// |T| or |fasync::future_output_t<F>| is the type of value produced when the future completes.
//
// |FASYNC::TRY_FUTURE<E, T>| SYNOPSIS
//
// |E| or |fasync::future_error_t<F>| is the type of value produced when the future completes with
// an error.
//
// |T| or |fasync::future_value_t<F>| is the type of value produced when the future completes
// successfully. When the underlying |fit::result| has no |::value_type|, then there is no future
// value type either.
//
// FASYNC::FUTURE AND FASYNC::TRY_FUTURE
//
// For the rest of this document, references to |fasync::future| should be taken to apply to both
// |fasync::future| and |fasync::try_future|. |fasync::future| produces a bare value (which makes it
// easier for us to interact with C++20 coroutines going forward) and |fasync::try_future| is simply
// a typedef for an |fasync::future| that produces a |fit::result|, which is necessary to use
// combinators like |fasync::and_then()|, |fasync::or_else()|, etc.
//
// The word "output" is used to refer to the type or value produced by the bare future, which for an
// |fasync::try_future| is a |fit::result|. The word "value" is used (mostly) to refer to the
// success value produced by an |fasync::try_future|, and the word "error" is used to refer to the
// error value produced by an |fasync::try_future|.
//
// CHAINING FUTURES USING COMBINATORS
//
// Futures can be chained together using combinators such as |fasync::then()| which consume the
// original future(s) and return a new combined future.
//
// For example, the |fasync::then()| combinator returns a future that has the effect of
// asynchronously awaiting completion of the prior future (the instance upon which |fasync::then()|
// was called) then delivering its result to a handler function.
//
// Available combinators defined in this library:
//
//    |fasync::map()|: run a handler when prior future completes
//    |fasync::map_ok()|: run a handler when prior future completes successfully
//    |fasync::map_error()|: run a handler when prior future completes with an error
//    |fasync::flatten()|: turn a nested future into one with one less layer of nesting
//    |fasync::flatten_all()|: turn a nested future into one with all nesting removed up to the
//                             encounter of a non-future return type
//    |fasync::then()|: run a handler when prior future completes, unwrapping returned futures
//    |fasync::and_then()|: run a handler when prior future completes successfully, unwrapping
//                          returned futures
//    |fasync::or_else()|: run a handler when prior future completes with an error, unwrapping
//                         returned futures
//    |fasync::inspect()|: examine result of prior future
//    |fasync::inspect_ok()|: examine successful result of prior future
//    |fasync::inspect_error()|: examine error result of prior future
//    |fasync::discard()|: discard result and unconditionally return void when prior future
//                         completes
//    |fasync::wrap_with()|: applies a wrapper to the future
//    |fasync::box()|: wraps the future's continuation into a |fit::function|
//    |fasync::join()|: await multiple futures in an argument list, once they all complete return
//                      a corresponding container of their outputs.
//    |fasync::join_with()|: like |fasync::join()|, but can be used in the middle of a pipeline
//    |fasync::schedule_on()|: schedules the future for execution on the given executor
//    |fasync::block_on()|: blocks the current thread to execute the future on the given executor.
//
// You can also create your own custom combinators by crafting new types of continuations.
//
// CONTINUATIONS AND HANDLERS
//
// Internally, |fasync::future| wraps a continuation (a kind of callable object) that holds the
// state of the asynchronous task and provides a means for making progress through repeated
// invocation.
//
// A future's continuation is generated through the use of factories such as |fasync::make_future()|
// and combinators such as |fasync::then()|. Most of these functions accept a client-supplied
// "handler" (another kind of callable object, often a lambda expression) which performs the actual
// computations.
//
// Continuations have a very regular interface: they always accept a |fasync::context&| argument
// and return a |fasync::poll|.  Conversely, handlers have a very flexible interface: clients can
// provide them in many forms all of which are documented by the individual functions which consume
// them. It's pretty easy to use: the library takes care of wrapping client-supplied handlers of all
// supported forms into the continuations it uses internally.
//
// HANDLER TYPES
//
// |fasync::future| is flexible to a fault with respect to acceptable handler signatures; the old
// |fpromise::promise| was very rigid about this due to an outdated callable traits library, but
// |fasync::future| is fully `INVOKE()`-aware (see [func.require] in C++17 and later). This means
// that not only can auto be used for parameter types, but so can variadics and even fancier things
// can happen with automatic destructuring and more.
//
// Here is a situation in the old |fpromise::promise|:
//
//     auto get_random_number() {
//         return fpromise::make_promise([] { return rand() % 10 });
//     }
//
//     auto get_random_product() {
//         auto f = get_random_number();
//         auto g = get_random_number();
//         return fpromise::join_promises(std::move(f), std::move(g))
//             .and_then([] (std::tuple<fpromise::result<int>, fpromise::result<int>>& results) {
//                 return fpromise::ok(results.get<0>.value() + results.get<1>.value());
//             });
//     }
//
// See how the handler must explicitly take a reference to a tuple of results; imagine how much
// worse it would be with three or more results or results with errors!
//
// Here is a spiced up example with |fasync::future|:
//
//     auto get_random_number() {
//       return fasync::make_future([] { return fit::ok(rand() % 10); });
//     }
//
//     auto get_random_product() {
//         return fasync::join(get_random_number(), get_random_number(), get_random_number()) |
//                fasync::then([](auto& result1, auto& result2, auto& result3) {
//                  return fit::ok(result1.value() + result2.value() + result3.value());
//                });
//     }
//
// We can get even more ergonomic with an arbitrary number of futures and C++17 fold expressions:
//
//     auto get_random_product() {
//         return fasync::join(get_random_number(), get_random_number(), get_random_number()) |
//                fasync::then([](auto&... results) {
//                  return fit::ok((results.value() + ...));
//                });
//     }
//
// In general a handler can take its argument by any of:
// - T&
// - const T&
// - auto (if copyable)
// - auto&
// - const auto&
// - auto&& (sometimes advisable as a "universal reference")
// - auto&... and other parameter pack variations on the above
//
// Handlers are also allowed to ignore all their parameters.
//
// In addition, a handler can take an optional |fasync::context&| or |const fasync::context&|
// parameter as the first argument, for example in order to suspend itself. Acceptable return types
// are be documented on the corresponding methods.
//
// THEORY OF OPERATION
//
// On its own, a future is "inert"; it only makes progress in response to actions taken by its
// owner. The state of the future never changes spontaneously or concurrently.
//
// Typically, a future is executed by wrapping it into a |fasync::pending_task| and scheduling it
// for execution using |fasync::executor::schedule()| or |fasync::schedule_on|. A future's
// |operator(fasync::context&)| can also be invoked directly by its owner from within the scope of
// another task (this is used to implement combinators and futures) though the principle is the
// same.
//
// |fasync::executor| is an abstract class that encapsulates a strategy for executing tasks. The
// executor is responsible for invoking each tasks's continuation until the task returns a
// non-pending result, indicating that the task has been completed.
//
// The method of execution and scheduling of each continuation call is left to the discretion of
// each executor implementation. Typical executor implementations may dispatch tasks on an
// event-driven message loop or on a thread pool. Developers are responsible for selecting
// appropriate executor implementations for their programs.
//
// During each invocation, the executor passes the continuation an execution context object
// represented by a subclass of |fasync::context|. The continuation attempts to make progress then
// returns a value of type |fasync::poll| to indicate whether it completed (signaled by
// |fasync::ready|) or was unable to complete the task during that invocation (signaled by
// |fasync::pending|). For example, a continuation may be unable to complete the task if it must
// asynchronously await completion of an I/O or IPC operation before it can proceed any further.
//
// If the continuation was unable to complete the task during its invocation, it may to call
// |fasync::context::suspend_task()| to acquire a |fasync::suspended_task| object. The continuation
// then arranges for the task to be resumed asynchronously (with |fasync::suspended_task::resume()|)
// once it becomes possible for the future to make forward progress again. Finally, the continuation
// returns returns |fasync::pending()| to indicate to the executor that it was unable to complete
// the task during that invocation.
//
// When the executor receives a pending result from a task's continuation, it moves the task into a
// table of suspended tasks. A suspended task is considered abandoned if has not been resumed and
// all remaining |fasync::suspended_task| handles representing it have been dropped. When a task is
// abandoned, the executor removes it from its table of suspended tasks and destroys the task
// because it is not possible for the task to be resumed or to make progress from that state.
//
// See also |fasync::single_threaded_executor| for a simple executor implementation.
//
// BOXED AND UNBOXED FUTURES
//
// To make combination and execution as efficient as possible, the futures returned by
// |fasync::make_future| and by combinators are parameterized by complicated continuation types
// that are hard to describe, often consisting of nested templates and lambdas. These are referred
// to as "unboxed" futures. In contrast, "boxed" futures are parameterized by a |fit::function| that
// hides (or "erases") the type of the continuation thereby yielding type that is easier to
// describe.
//
// You can recognize boxed and unboxed futures by their types.
// Here are two examples:
//
// - A boxed future type: |fasync::future<>| which is an alias for
//  |fasync::unboxed_future<fit::function<fasync::poll<>(fasync::context&)>|.
// - An unboxed future type: |fasync::unboxed_future<
//   fasync::internal::then_future<...something unintelligible...>>|
//
// Even though |fasync::unboxed_future| shows up in the type of the example boxed future, the
// presence of |fit::function| makes it boxed, and the presence of fasync::unboxed_future here is
// normally not discovered by the user.
//
// Although boxed futures are easier to manipulate, they may cause the continuation to be allocated
// on the heap. Chaining boxed futures can result in multiple allocations being produced.
//
// Conversely, unboxed futures have full type information. Not only does this defer heap allocation
// but it also makes it easier for the C++ compiler to fuse a chains of unboxed futures together
// into a single object that is easier to optimize.
//
// Unboxed futures can be boxed by assigning them to a boxed future type (such as
// |fasync::future<>|) or using the |fasync::box()| combinator.
//
// As a rule of thumb, always defer boxing of futures until it is necessary to transport them using
// a simpler type.
//
// Do this: (chaining as a single expression performs at most one heap allocation)
//
//     fasync::future<> f = fasync::make_future([] { ... }) |
//         fasync::then([](fit::result<fit::failed>& result) { ... }) |
//         fasync::and_then([] { ... });
//
// Or this: (still only performs at most one heap allocation)
//
//     auto f = fasync::make_future([] { ... });
//     auto g = fasync::then(std::move(f), [](fit::result<fit::failed>& result) { ... });
//     auto h = fasync::and_then(std::move(g), [] { ... });
//     fasync::future<> boxed_h = h;
//
// But don't do this: (incurs up to three heap allocations due to eager boxing)
//
//     fasync::try_future<fit::failed> f = fasync::make_future([] { ... });
//     fasync::try_future<fit::failed> g = fasync::then(
//                                              std::move(f),
//                                              [](fit::result<fit::failed>& result) { ... });
//     fasync::try_future<fit::failed> h = fasync::and_then(std::move(g), [] { ... });
//
// SINGLE OWNERSHIP MODEL
//
// Futures have a single-ownership semantics. This means that there can only be at most onex
// reference to the task represented by its continuation along with any state held by that
// continuation.
//
// When a combinator is applied to a future, ownership of its continuation is transferred to the
// combined future, leaving the original future in an "empty" state without a continuation. Note
// that it is an error to attempt to invoke an empty future.
//
// This model greatly simplifies reasoning about object lifetime. If a future goes out of scope
// without completing its task, the task is considered "abandoned", causing all associated state to
// be destroyed.
//
// Note that a future may capture references to other objects whose lifetime differs from that of
// the future. It is the responsibility of the future to ensure reachability of the objects whose
// reference it captures such as by using reference counted pointers, weak pointers, or other
// appropriate mechanisms to ensure memory safety.
//
// THREADING MODEL
//
// Future objects are not thread-safe themselves. You cannot call their methods concurrently (or
// re-entrantly). However, futures can safely be moved to other threads and executed there (unless
// their continuation requires thread affinity for some reason but that's beyond the scope
// of this document).
//
// This property of being thread-independent, combined with the single ownership model, greatly
// simplifies the implementation of thread pool based executors.
//
// RESULT RETENTION
//
// A future's continuation can only be executed to completion once. After it completes, it cannot be
// run again.
//
// This method of execution is very efficient; the future's result is returned directly to its
// invoker; it is not copied or retained within the future object itself. It is entirely the
// caller's responsibility to decide how to consume or retain the result if need be.
//
// CLARIFICATION OF NOMENCLATURE
//
// In this library, the word "future" has the following definition:
//
// - A *future* holds the function that performs an asynchronous task.
//   It is the means to produce a value.
//
// Be aware that other libraries may use this term slightly differently.
//
// For more information about the theory of futures and promises, see
// https://en.wikipedia.org/wiki/Futures_and_promises.
//
// COMPARISON WITH STD::FUTURE
//
// |std::future| provides a mechanism for running asynchronous tasks and awaiting their results on
// other threads.  Waiting can be performed either by blocking the waiting thread or by polling the
// future. The manner in which tasks are scheduled and executed is entirely controlled by the C++
// standard library and offers limited control to developers.
//
// |fasync::future| and |fasync::try_future| provide a mechanism for running asynchronous tasks,
// chaining additional tasks using combinators, and awaiting their results. An executor is
// responsible for suspending tasks awaiting results of other tasks and is at liberty to run other
// tasks on the same thread rather than blocking.  In addition, developers can create custom
// executors to implement their own policies for running tasks.
//
// Decoupling awaiting from blocking makes |fasync::future| quite versatile. |fasync::future| can
// also interoperate with other task dispatching mechanisms (including |std::future|) using
// adapters such as |fasync::bridge|.
//
// EXAMPLE
//
// -
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/lib/fasync/tests/future_example.cc

// Make a future that immediately resolves with the given value.
template <typename T>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::value_future<T> make_value_future(T&& value) {
  return ::fasync::internal::value_future<T>(std::forward<T>(value));
}

// Same as above, but construct in-place.
template <typename T, typename... Args>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::value_future<T> make_value_future(
    Args&&... args) {
  return ::fasync::internal::value_future<T>(std::forward<Args>(args)...);
}

// Make a future that immediately resolves with a |fit::result|.
template <typename E, typename... Ts>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::result_future<E, Ts...> make_try_future(
    ::fit::result<E, Ts...>&& r) {
  return ::fasync::internal::result_future<E, Ts...>(std::forward<::fit::result<E, Ts...>>(r));
}

// Construct in-place.
template <typename E, typename... Ts, typename... Args>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::result_future<E, Ts...> make_try_future(
    Args&&... args) {
  return ::fasync::internal::result_future<E, Ts...>(std::forward<Args>(args)...);
}

// Make a future that resolves with |fit::result<fit::failed>|, bearing the ok value.
LIB_FASYNC_NODISCARD inline constexpr ::fasync::internal::ok_future<> make_ok_future() {
  return ::fasync::internal::ok_future<>(::fit::ok());
}

// Make a future that resolves with |fit::result<fit::failed, T>|, bearing the T value.
template <typename T>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::ok_future<T> make_ok_future(T&& value) {
  return ::fasync::internal::ok_future<T>(::fit::ok(std::forward<T>(value)));
}

// Construct in-place.
template <typename T, typename... Args>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::ok_future<T> make_ok_future(Args&&... args) {
  return ::fasync::internal::ok_future<T>(::fit::ok(std::forward<Args>(args)...));
}

// Make a future that resolves with |fit::result<E>|, bearing the E value.
template <typename E>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::error_future<E> make_error_future(E&& e) {
  return ::fasync::internal::error_future<E>(::fit::as_error(std::forward<E>(e)));
}

// Construct in-place.
template <typename E, typename... Args>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::error_future<E> make_error_future(
    Args&&... args) {
  return ::fasync::internal::error_future<E>(::fit::as_error(std::forward<Args>(args)...));
}

// Make a future that resolves to |fit::result<fit::failed>|, bearing the failed value.
LIB_FASYNC_NODISCARD inline constexpr ::fasync::internal::failed_future make_failed_future() {
  return ::fasync::internal::failed_future(::fit::failed());
}

// Make a future whose poll type is |fasync::poll<Ts...>| but always returns pending.
template <typename... Ts>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::pending_future<Ts...> make_pending_future() {
  return ::fasync::internal::pending_future<Ts...>();
}

// Make a future whose poll type is |fasync::try_poll<E, Ts...>| but always returns pending.
template <typename E, typename... Ts>
LIB_FASYNC_NODISCARD constexpr ::fasync::internal::pending_try_future<E, Ts...>
make_pending_try_future() {
  return ::fasync::internal::pending_try_future<E, Ts...>();
}

// |fasync::unboxed_future<F>|
//
// Can hold any type for which |fasync::is_future_v<F>| holds, and is a future itself.
template <typename F>
class unboxed_future {
  static_assert(is_future_v<F>,
                "|fasync::unboxed_future<T>| must hold a valid future type for which "
                "|fasync::is_future_v<T>| is true.");
  static_assert(std::is_same_v<F, std::decay_t<F>>,
                "F may not be a reference, array, or function type.");

 public:
  constexpr unboxed_future() = delete;

  constexpr unboxed_future(const unboxed_future&) = delete;
  constexpr unboxed_future& operator=(const unboxed_future&) = delete;
  constexpr unboxed_future(unboxed_future&&) = default;
  constexpr unboxed_future& operator=(unboxed_future&&) = default;

  template <typename G, ::fasync::internal::requires_conditions<std::is_constructible<F, G>> = true>
  constexpr unboxed_future(G&& future) : future_(std::forward<G>(future)) {}

  // Allow throwing away the output of the given future; important for e.g. constructing an
  // |fasync::future<>| from anything like in |fasync::pending_task|.
  // Implemented with lambdas because |fasync::discard| is not defined yet.
  // TODO(schottm): allow throwing away success or error with try_futures?
  template <
      typename FF = F, typename G,
      ::fasync::internal::requires_conditions<is_void_future<FF>, is_future<G>,
                                              cpp17::negation<std::is_constructible<F, G>>> = true>
  constexpr unboxed_future(G&& future)
      : future_(::fasync::internal::discard_future<G>(std::forward<G>(future))) {}

  template <typename G, ::fasync::internal::requires_conditions<cpp17::negation<std::is_same<F, G>>,
                                                                std::is_constructible<F, G>> = true>
  constexpr unboxed_future(unboxed_future<G>&& other) : future_(std::move(other.future_)) {}

  // Invokes the future's continuation.
  //
  // This method should be called by an executor to evaluate the future. If the |poll.is_pending()|
  // is true, then the executor is responsible for arranging to invoke the future again once it
  // determines that it is possible to make progress towards completion of the encapsulated future.
  //
  // Once the continuation returns an |fasync::poll| with status |poll.is_ready()|, the future
  // should not be invoked again.
  //
  // The future must be non-empty.
  constexpr future_poll_t<F> operator()(context& context) {
    return cpp20::invoke(future_, context);
  }

 private:
  template <typename G>
  friend class unboxed_future;

  F future_;
};

#if LIB_FASYNC_HAS_CPP_FEATURE(deduction_guides)

template <typename F>
unboxed_future(F&&) -> unboxed_future<std::decay_t<F>>;

template <typename F>
unboxed_future(unboxed_future<F>&&) -> unboxed_future<F>;

#endif

// Type-erased future type, usually heap-allocated.
template <typename... Ts>
using future = unboxed_future<::fit::function<poll<Ts...>(context&)>>;

// Same but for |fit::result|.
template <typename E = ::fit::failed, typename... Ts>
using try_future = future<::fit::result<E, Ts...>>;

// |fasync::pending_task|
//
// A pending task holds an |fasync::future| that can be scheduled to run on an |fasync::executor|
// using |fasync::executor::schedule_task()|.
//
// An executor repeatedly invokes a pending task until it returns true, indicating completion. Note
// that the future's resulting value or error is discarded since it is not meaningful to the
// executor. If you need to consume the result, use a combinator such as |fasync::then()| to capture
// it prior to wrapping the future into a pending task.
//
// See documentation of |fasync::future| for more information.
class pending_task final {
 public:
  // The type of future held by this task.
  using future_type = future<>;

  pending_task() = delete;

  pending_task(const pending_task&) = delete;
  pending_task& operator=(const pending_task&) = delete;
  pending_task(pending_task&&) = default;
  pending_task& operator=(pending_task&&) = default;

  // Destroys the pending task, releasing its future.
  ~pending_task() = default;

  // Creates a pending task that wraps any kind of future, boxed or unboxed, regardless of its
  // result type and with any context that is assignable from this task's context type.
  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  pending_task(F&& future) : future_(std::forward<F>(future)) {}

  // Evaluates the pending task.
  // If the task completes (returns a non-pending result), the task returns true and must not be
  // invoked again.
  bool operator()(fasync::context& context) { return !future_(context).is_pending(); }

  // Extracts the pending task's future.
  future_type take_future() { return std::move(future_); }

 private:
  future_type future_;
};

// |fasync::executor|
//
// An abstract interface for executing asynchronous tasks, such as futures, represented by
// |fasync::pending_task|.
//
// EXECUTING TASKS
//
// An executor evaluates its tasks incrementally.  During each iteration of the executor's main
// loop, it invokes the next task from its ready queue.
//
// If the task returns true, then the task is deemed to have completed. The executor removes the
// tasks from its queue and destroys it since there is nothing left to do.
//
// If the task returns false, then the task is deemed to have voluntarily suspended itself pending
// some event that it is awaiting.  Prior to returning, the task should acquire at least one
// |fasync::suspended_task| handle from its execution context using
// |fasync::context::suspend_task()| to provide a means for the task to be resumed once it can make
// forward progress again.
//
// Once the suspended task is resumed with |fasync::suspended_task::resume()|, it is moved back to
// the ready queue and it will be invoked again during a later iteration of the executor's loop.
//
// If all |fasync::suspended_task| handles for a given task are destroyed without the task ever
// being resumed then the task is also destroyed since there would be no way for the task to be
// resumed from suspension.  We say that such a task has been "abandoned".
//
// The executor retains single-ownership of all active and suspended tasks. When the executor is
// destroyed, all of its remaining tasks are also destroyed.
//
// Please read |fasync::future| for a more detailed explanation of the responsibilities of tasks
// and executors.
//
// NOTES FOR IMPLEMENTORS
//
// This interface is designed to support a variety of different executor implementations. For
// example, one implementation might run its tasks on a single thread whereas another might dispatch
// them on an event-driven message loop or use a thread pool.
//
// See also |fasync::single_threaded_executor| for a concrete implementation.
class executor {
 public:
  // Destroys the executor along with all of its remaining scheduled tasks that have yet to
  // complete.
  virtual ~executor() = default;

  // Schedules a task for eventual execution by the executor.
  //
  // This method is thread-safe.
  virtual void schedule(pending_task&& task) = 0;
};

// |fasync::suspended_task|
//
// Represents a task that is awaiting resumption.
//
// This object has RAII semantics. If the task is not resumed by at least one holder of its
// |suspended_task| handles, then it will be destroyed by the executor since it is no longer
// possible for the task to make progress. The task is said have been "abandoned".
//
// See documentation of |fasync::executor| for more information.
class suspended_task final {
 public:
  // A handle that grants the capability to resume a suspended task. Each issued ticket must be
  // individually resolved.
  using ticket = uint64_t;

  // The resolver mechanism implements a lightweight form of reference counting for tasks that have
  // been suspended.
  //
  // When a suspended task is created in a non-empty state, it receives a pointer to a resolver
  // interface and a ticket. The ticket is a one-time-use handle that represents the task that was
  // suspended and provides a means to resume it. The |suspended_task| class ensures that every
  // ticket is precisely accounted for.
  //
  // When |suspended_task::resume()| is called on an instance with a valid ticket, the resolver's
  // |resolve_ticket()| method is invoked passing the ticket's value along with *true* to resume
  // the task. This operation consumes the ticket so the |suspended_task| transitions to an empty
  // state. The ticket and resolver cannot be used again by this |suspended_task| instance.
  //
  // Similarly, when |suspended_task::reset()| is called on an instance with a valid ticket or when
  // the task goes out of scope on such an instance, the resolver's |resolve_ticket()| method is
  // invoked but this time passes *false* to not resume the task. As before, the ticket is consumed.
  //
  // Finally, when the |suspended_task| is copied, its ticket is duplicated using
  // |duplicate_ticket()| resulting in two tickets, both of which must be individually resolved.
  //
  // Resuming a task that has already been resumed has no effect. Conversely, a task is considered
  // "abandoned" if all of its tickets have been resolved without it ever being resumed. See
  // documentation of |fasync::future| for more information.
  //
  // The methods of this class are safe to call from any thread, including threads that may not be
  // managed by the task's executor.
  class resolver {
   public:
    // Duplicates the provided ticket, returning a new ticket.
    // Note: The new ticket may have the same numeric value as the original ticket but should be
    //       considered a distinct instance that must be separately resolved.
    virtual ticket duplicate_ticket(ticket ticket) = 0;

    // Consumes the provided ticket, optionally resuming its associated task. The provided ticket
    // must not be used again.
    virtual void resolve_ticket(ticket ticket, bool resume_task) = 0;

   protected:
    virtual ~resolver() = default;
  };

  suspended_task() : resolver_(nullptr), ticket_(0) {}

  suspended_task(resolver& resolver, ticket ticket) : resolver_(&resolver), ticket_(ticket) {}

  suspended_task(const suspended_task& other)
      : resolver_(other.resolver_),
        ticket_(resolver_ ? resolver_->duplicate_ticket(other.ticket_) : 0) {}

  suspended_task(suspended_task&& other) : resolver_(other.resolver_), ticket_(other.ticket_) {
    other.resolver_ = nullptr;
  }

  // Releases the task without resumption.
  //
  // Does nothing if this object does not hold a ticket.
  ~suspended_task() { reset(); }

  // Returns true if this object holds a ticket for a suspended task.
  explicit operator bool() const { return resolver_ != nullptr; }

  // Asks the task's executor to resume execution of the suspended task if it has not already been
  // resumed or completed. Also releases the task's ticket as a side-effect.
  //
  // Clients should call this method when it is possible for the task to make progress; for example,
  // because some event the task was awaiting has occurred.  See documentation on
  // |fasync::executor|.
  //
  // Does nothing if this object does not hold a ticket.
  void resume() { resolve(true); }

  // Releases the suspended task without resumption.
  //
  // Does nothing if this object does not hold a ticket.
  void reset() { resolve(false); }

  // Swaps suspended tasks.
  void swap(suspended_task& other) {
    if (this != &other) {
      using std::swap;
      swap(resolver_, other.resolver_);
      swap(ticket_, other.ticket_);
    }
  }

  suspended_task& operator=(const suspended_task& other) {
    if (this != &other) {
      reset();
      resolver_ = other.resolver_;
      ticket_ = resolver_ ? resolver_->duplicate_ticket(other.ticket_) : 0;
    }
    return *this;
  }

  suspended_task& operator=(suspended_task&& other) {
    if (this != &other) {
      reset();
      resolver_ = other.resolver_;
      ticket_ = other.ticket_;
      other.resolver_ = nullptr;
    }
    return *this;
  }

 private:
  void resolve(bool resume_task) {
    if (resolver_) {
      // Move the ticket to the stack to guard against possible re-entrance occurring as a
      // side-effect of the task's own destructor running.
      resolver* cached_resolver = resolver_;
      ticket cached_ticket = ticket_;
      resolver_ = nullptr;
      cached_resolver->resolve_ticket(cached_ticket, resume_task);
    }
  }

  resolver* resolver_;
  ticket ticket_;
};

inline void swap(suspended_task& a, suspended_task& b) { a.swap(b); }

// |fasync::context|
//
// Execution context for an asynchronous task, such as a |fasync::future| or |fasync::pending_task|.
//
// When a |fasync::executor| executes a task, it provides the task with an execution context which
// enables the task to communicate with the executor and manage its own lifecycle. Specialized
// executors may subclass |fasync::context| and offer additional methods beyond those which are
// defined here, such as to provide access to platform-specific features supported by the executor.
//
// The context provided to a task is only valid within the scope of a single invocation; the task
// must not retain a reference to the context across invocations.
//
// See documentation of |fasync::future| for more information.
class context {
 public:
  // Gets the executor that is running the task.
  virtual executor& executor() const = 0;

  // Obtains a handle that can be used to resume the task after it has been suspended.
  //
  // Clients should call this method before returning |fasync::pending()| from the task. See
  // documentation on |fasync::executor|.
  virtual suspended_task suspend_task() = 0;

  // Converts this context to a derived context type.
  template <typename C, ::fasync::internal::requires_conditions<std::is_base_of<context, C>> = true>
  C& as() & {
    // TODO(fxbug.dev/4060): We should perform a run-time type check here rather
    // than blindly casting.  That's why this method exists.
    return static_cast<C&>(*this);
  }

  // protected:
  virtual ~context() = default;
};

namespace internal {

template <typename F, typename G>
LIB_FASYNC_NODISCARD constexpr compose_wrapper_t<F, G> compose(F&& f, G&& g) {
  return compose_wrapper_t<F, G>(std::forward<F>(f), std::forward<G>(g));
}

// |future_adaptor_closure|
//
// This is the bread and butter of our |operator|()| composition, a curiously recurring template
// pattern (CRTP) class that adds the |operator|()| functionality to our closure objects. This
// allows both taking a future from the left (the normal method of operation) and pre-composing
// adaptors from the right.
template <typename T>
struct future_adaptor_closure {
  template <typename Future, typename Closure,
            requires_conditions<is_future<Future>, is_future_adaptor_closure<Closure>,
                                std::is_same<T, cpp20::remove_cvref_t<Closure>>,
                                cpp17::is_invocable<Closure, Future>> = true>
  friend constexpr decltype(auto) operator|(Future&& future, Closure&& closure) noexcept(
      std::is_nothrow_invocable_v<Closure, Future>) {
    return cpp20::invoke(std::forward<Closure>(closure), std::forward<Future>(future));
  }

  template <typename Closure, typename OtherClosure,
            requires_conditions<
                is_future_adaptor_closure<Closure>, is_future_adaptor_closure<OtherClosure>,
                std::is_same<T, cpp20::remove_cvref_t<Closure>>,
                std::is_constructible<std::decay_t<Closure>, Closure>,
                std::is_constructible<std::decay_t<OtherClosure>, OtherClosure>> = true>
  LIB_FASYNC_NODISCARD friend constexpr auto operator|(Closure&& c1, OtherClosure&& c2) noexcept(
      std::is_nothrow_constructible_v<std::decay_t<Closure>, Closure>&&
          std::is_nothrow_constructible_v<std::decay_t<OtherClosure>, OtherClosure>) {
    return future_adaptor_closure_t<compose_wrapper_t<OtherClosure, Closure>>(
        compose(std::forward<OtherClosure>(c2), std::forward<Closure>(c1)));
  }
};

// |combinator|
//
// This CRTP class allows us to factor out common combinator functionality via a private |invoke()|
// method that is exposed via friendship to this class. For combinators that take a future as a
// first argument and want to return a closure taking the other arguments as another overload, this
// class takes care of all that.
template <typename Derived>
class combinator {
 public:
  constexpr combinator() = default;

  constexpr combinator(const combinator&) = default;
  constexpr combinator& operator=(const combinator&) = default;
  constexpr combinator(combinator&&) = default;
  constexpr combinator& operator=(combinator&&) = default;

  // We don't use |requires_conditions| on the future because of hard failures during
  // substitution/overload resolution.
  template <typename Invoker = Derived, typename F, typename... Args>
  constexpr auto operator()(F&& future, Args&&... args) const
      -> decltype(Invoker::invoke(std::forward<F>(future), std::forward<Args>(args)...)) {
    static_assert(is_future_v<F>, "");
    return Invoker::invoke(std::forward<F>(future), std::forward<Args>(args)...);
  }

  template <typename... Args>
  constexpr auto operator()(Args&&... args) const {
    return closure<Derived, Args...>(std::forward<Args>(args)...);
  }

 private:
  template <typename Combinator, typename... GivenArgs>
  class closure final : future_adaptor_closure<closure<Combinator, GivenArgs...>> {
   public:
    template <typename... Args,
              requires_conditions<std::is_constructible<GivenArgs, Args>...> = true>
    explicit constexpr closure(Args&&... args) : given_args_(std::forward<Args>(args)...) {}

    template <typename F, requires_conditions<is_future<F>> = true>
    constexpr auto operator()(F&& future) {
      return cpp17::apply(
          [&](auto&&... args) {
            return cpp20::invoke(&Combinator::template invoke<F, decltype(args)...>,
                                 std::forward<F>(future), std::forward<decltype(args)>(args)...);
          },
          std::move(given_args_));
    }

   private:
    std::tuple<std::decay_t<GivenArgs>...> given_args_;
  };
};

}  // namespace internal

// |fasync::make_future|
//
// Allows making futures out of all different kinds of callables, returning bare values,
// |fasync::pending|, |fasync::ready|, or other futures.
//
// Allowed handler argument types:
// - no arguments
// - fasync::context&
//
// Allowed handler return types:
// - void
// - any arbitrary type allowed in an |fasync::poll|
// - any unboxed future
// - fasync::pending
// - fasync::ready
// - fasync::poll
template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
LIB_FASYNC_NODISCARD constexpr F make_future(F&& f) {
  return std::forward<F>(f);
}

template <typename F, ::fasync::internal::requires_conditions<cpp17::negation<is_future<F>>> = true>
LIB_FASYNC_NODISCARD constexpr auto make_future(F&& fun) {
  return ::fasync::internal::make_future_adaptor_t<F>(std::forward<F>(fun));
}

namespace internal {

template <typename E, requires_conditions<is_executor<E>> = true>
class schedule_on_closure final : future_adaptor_closure<schedule_on_closure<E>> {
 public:
  template <typename F, requires_conditions<std::is_constructible<E&, F&>> = true>
  explicit constexpr schedule_on_closure(F& executor) : executor_(executor) {}

  template <typename F, requires_conditions<is_future<F>> = true>
  constexpr void operator()(F&& future) {
    executor_.schedule(std::forward<F>(future));
  }

 private:
  E& executor_;
};

template <typename E>
using schedule_on_closure_t = schedule_on_closure<std::decay_t<E>>;

class schedule_on_combinator final {
 public:
  template <typename F, typename E, requires_conditions<is_future<F>, is_executor<E>> = true>
  constexpr void operator()(F&& future, E& executor) const {
    executor.schedule(std::forward<F>(future));
  }

  template <typename E, requires_conditions<is_executor<E>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(E& executor) const {
    return schedule_on_closure_t<E>(executor);
  }
};

}  // namespace internal

// |fasync::schedule_on|
//
// Will be the end of many pipelines in the |fasync::future| world. In its pipelined form, it sits
// after the bar and takes a reference to an executor, which must outlive the execution of the
// future pipeline on the left. The future to the left of the bar is then scheduled asynchronously
// for execution on that executor.
//
// Call pattern:
// - fasync::schedule_on(<future>, <executor>)
// - <future> | fasync::schedule_on(<executor>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::schedule_on_combinator schedule_on;

namespace internal {

class map_combinator final : public combinator<map_combinator> {
 private:
  friend class combinator<map_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::map|
//
// Perhaps the most useful basic combinator, allowing one to supply a handler to consume the output
// of the given future and returning a wrapped future that returns the transformed output.
//
// Call pattern:
// - fasync::map(<future>, <handler>)
// - <future> | fasync::map(<handler>)
//
// See |fasync::then| for a list of valid handler types (other than returning futures).
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::map_combinator map;

namespace internal {

class map_ok_combinator final : public combinator<map_ok_combinator> {
 private:
  friend class combinator<map_ok_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_ok_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::map_ok|
//
// Like |fasync::map|, but acts on the |.value()| of a |fit::result| returned by the previous
// future.
//
// Call pattern:
// - fasync::map_ok(<result returning future>, <handler>)
// - <result returning future> | fasync::map_ok(<handler>)
//
// See |fasync::and_then| for a list of valid handler types (other than returning futures).
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::map_ok_combinator map_ok;

namespace internal {

class map_error_combinator final : public combinator<map_error_combinator> {
 private:
  friend class combinator<map_error_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_error_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::map_error|
//
// Like |fasync::map_ok|, but acts on the |.error_value()| of a |fit::result| returned by the
// previous future.
//
// Call pattern:
// - fasync::map_error(<result returning future>, <handler>)
// - <result returning future> | fasync::map_error(<handler>)
//
// See |fasync::or_else| for a list of valid handler types (other than returning futures).
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::map_error_combinator map_error;

namespace internal {

class inspect_combinator final : public combinator<inspect_combinator> {
 private:
  friend class combinator<inspect_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return inspect_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::inspect|
//
// Takes a future |F| and a callable that takes |const fasync::future_output_t<F>&|, so that the
// callable can inspect the value provided by the future before passing it on to the next
// combinator in the chain. The callable must return |void|.
//
// Call pattern:
// - fasync::inspect(<future>, <handler)
// - <future> | fasync::inspect(<handler>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::inspect_combinator inspect;

namespace internal {

class inspect_ok_combinator final : public combinator<inspect_ok_combinator> {
 private:
  friend class combinator<inspect_ok_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return inspect_ok_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::inspect_ok|
//
// Like |fasync::inspect|, but acts on the |.value()| from the |fit::result| of an
// |fasync::try_future|. Again, the callable must return |void|.
//
// Call pattern:
// - fasync::inspect_ok(<result returning future>, <handler>)
// - <result returning future> | fasync::inspect_ok(<handler>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::inspect_ok_combinator inspect_ok;

namespace internal {

class inspect_error_combinator final : public combinator<inspect_error_combinator> {
 private:
  friend class combinator<inspect_error_combinator>;

  template <typename F, typename H>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return inspect_error_future_t<F, H>(std::forward<F>(future), std::forward<H>(handler));
  }
};

}  // namespace internal

// |fasync::inspect_error|
//
// Like |fasync::inspect_ok|, but acts on the |.error_value()| from the |fit::result| of an
// |fasync::try_future|. Again, the callable must return |void|.
//
// Call pattern:
// - fasync::inspect_error(<result returning future>, <handler>)
// - <result returning future> | fasync::inspect_error(<handler>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::inspect_error_combinator inspect_error;

namespace internal {

class discard_closure final : public future_adaptor_closure<discard_closure> {
 public:
  template <typename F, requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr discard_future_t<F> operator()(F&& future) const {
    return discard_future_t<F>(std::forward<F>(future));
  }
};

}  // namespace internal

// |fasync::discard|
//
// Turns any future into a wrapped future that discards the output, i.e. its |operator()| returns
// |fasync::poll<>|.
//
// Call pattern:
// - fasync::discard(<future>)
// - <future> | fasync::discard
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::discard_closure discard;

namespace internal {

class flatten_closure final : public future_adaptor_closure<flatten_closure> {
 public:
  template <typename F, requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr flatten_future_t<F> operator()(F&& future) const {
    return flatten_future_t<F>(std::forward<F>(future));
  }
};

}  // namespace internal

// |fasync::flatten|
//
// Takes nested futures, (i.e., futures returning an |fasync::poll| with another future inside) and
// removes one layer of nesting. For example, |fasync::future<fasync::future<int>>| would become
// |fasync::future<int>|.
//
// Call pattern:
// - fasync::flatten(<future returning future>)
// - <future returning future> | fasync::flatten
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::flatten_closure flatten;

namespace internal {

class flatten_all_closure final : public future_adaptor_closure<flatten_all_closure> {
 public:
  template <typename F, requires_conditions<cpp17::negation<is_future<future_output_t<F>>>> = true>
  LIB_FASYNC_NODISCARD constexpr F operator()(F&& future) const {
    return std::forward<F>(future);
  }

  template <typename F, requires_conditions<is_future<future_output_t<F>>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F&& future) const {
    return operator()(flatten(std::forward<F>(future)));
  }
};

}  // namespace internal

// |fasync::flatten_all|
//
// Like |fasync::flatten|, but all layers of nesting are removed if possible.
//
// Call pattern:
// - fasync::flatten_all(<future>)
// - <future> | fasync::flatten_all
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::flatten_all_closure flatten_all;

namespace internal {

class then_combinator final : public combinator<then_combinator> {
 private:
  friend class combinator<then_combinator>;

  template <typename F, typename H,
            requires_conditions<cpp17::negation<is_future<handler_output_t<H, F>>>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map(std::forward<F>(future), std::forward<H>(handler));
  }

  template <typename F, typename H, requires_conditions<is_future<handler_output_t<H, F>>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return flatten(map(std::forward<F>(future), std::forward<H>(handler)));
  }
};

}  // namespace internal

// |fasync::then|
//
// Returns an unboxed future which invokes the specified handler function after this future
// completes (successfully or unsuccessfully), passing its result.
//
// |handler| is a callable object (such as a lambda) which consumes the result of this future and
// returns a new result with any value type and error type. Must not be null.
//
// |fasync::then| is one of the most important combinators; it can act like |fasync::map| but also
// unwraps futures returned by the given handler, giving it monadic properties.
//
// The handler must return one of the following types:
// - void
// - T
// - fit::result<new_error_type, new_value_type>
// - fit::success<new_value_type>
// - fit::error<new_error_type>
// - fasync::pending
// - fasync::try_ready<new_error_type, new_value_type>
// - fasync::try_poll<new_error_type, new_value_type>
// - fasync::try_future<new_error_type, new_value_type>
// - any callable or unboxed future with the following signature:
//   T(fasync::context&)
//
// The handler must accept one of the following argument lists:
// - See above documentation at HANDLER TYPES and substitute T with result_type. Note that it can be
//   any type, not just a |fit::result|.
//
// Call pattern:
// - fasync::then(<future>, <handler>)
// - <future> | fasync::then(<handler>)
//
// This method consumes the future's continuation, leaving it empty.
//
// EXAMPLE
//
//     auto f = fasync::make_future(...) |
//         fasync::then([] (fit::result<std::string, int>& result)
//                   -> fit::result<fit::failed, std::string> {
//             if (result.is_ok()) {
//                 printf("received value: %d\n", result.value());
//                 if (result.value() % 15 == 0)
//                     return fit::ok("fizzbuzz");
//                 if (result.value() % 3 == 0)
//                     return fit::ok("fizz");
//                 if (result.value() % 5 == 0)
//                     return fit::ok("buzz");
//                 return fit::ok(std::to_string(result.value()));
//             } else {
//                 printf("received error: %s\n", result.error().c_str());
//                 return fit::failed();
//             }
//         }) |
//         fasync::then(...);
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::then_combinator then;

namespace internal {

class and_then_combinator final : public combinator<and_then_combinator> {
 private:
  friend class combinator<and_then_combinator>;

  template <typename F, typename H, typename P = handle_map_ok_t<H, F>,
            requires_conditions<cpp17::negation<is_value_try_poll<P>>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_ok(std::forward<F>(future), std::forward<H>(handler));
  }

  template <typename F, typename H, typename V = poll_value_t<handle_map_ok_t<H, F>>,
            requires_conditions<cpp17::negation<is_future<V>>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_ok(std::forward<F>(future), std::forward<H>(handler));
  }

  template <typename F, typename H, typename V = poll_value_t<handle_map_ok_t<H, F>>,
            requires_conditions<is_future<V>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return try_flatten_future_t<map_ok_future_t<F, H>>(
        map_ok(std::forward<F>(future), std::forward<H>(handler)));
  }
};

}  // namespace internal

// |fasync::and_then|
//
// Returns an unboxed future which invokes the specified handler function after this future
// completes successfully, passing its resulting value.
//
// |handler| is a callable object (such as a lambda) which consumes the result of this future and
// returns a new result with any value type but the same error type.  Must not be null.
//
// |fasync::and_then| is to |fasync::map_ok| as |fasync::then| is to |fasync::map|. That is, it can
// do what |fasync::map_ok| can do and also unwraps returned futures.
//
// The handler must return one of the following types:
// - void
// - fit::result<error_type, new_value_type>
// - fit::success<new_value_type>
// - fit::error<error_type>
// - fasync::pending
// - fasync::try_ready<error_type, new_value_type>
// - fasync::try_poll<error_type, new_value_type>
// - fasync::try_future<error_type, new_value_type>
// - any callable or unboxed future with the following signature:
//   fit::result<error_type, new_value_type>(fasync::context&)
//
// The handler must accept one of the following argument lists:
// - See above documentation at HANDLER TYPES and substitute T with value_type.
//
// Call pattern:
// - fasync::and_then(<result returning future>, <handler>)
// - <result returning future> | fasync::and_then(<handler>)
//
// This method consumes the future's continuation, leaving it empty.
//
// EXAMPLE
//
//     auto f = fasync::make_future(...) |
//         fasync::and_then([] (const int& value) {
//             printf("received value: %d\n", value);
//             if (value % 15 == 0)
//                 return fit::ok("fizzbuzz");
//             if (value % 3 == 0)
//                 return fit::ok("fizz");
//             if (value % 5 == 0)
//                 return fit::ok("buzz");
//             return fit::ok(std::to_string(value));
//         }) |
//         fasync::then(...);
//
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::and_then_combinator and_then;

namespace internal {

class or_else_combinator final : public combinator<or_else_combinator> {
 private:
  friend class combinator<or_else_combinator>;

  template <typename F, typename H, typename E = poll_error_t<handle_map_error_t<H, F>>,
            requires_conditions<cpp17::negation<is_future<E>>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return map_error(std::forward<F>(future), std::forward<H>(handler));
  }

  template <typename F, typename H, typename E = poll_error_t<handle_map_error_t<H, F>>,
            requires_conditions<is_future<E>> = true>
  LIB_FASYNC_NODISCARD static constexpr auto invoke(F&& future, H&& handler) {
    return try_flatten_error_future_t<map_error_future_t<F, H>>(
        map_error(std::forward<F>(future), std::forward<H>(handler)));
  }
};

}  // namespace internal

// |fasync::or_else|
//
// Returns an unboxed future which invokes the specified handler function after this future
// completes with an error, passing its resulting error.
//
// |handler| is a callable object (such as a lambda) which consumes the result of this future and
// returns a new result with any error type but the same value type. Must not be null.
//
// |fasync::or_else| is to |fasync::map_error| as |fasync::and_then| is to |fasync::map_ok|. That
// is, it can do what |fasync::map_error| can do and also unwraps returned futures.
//
// The handler must return one of the following types:
// - void
// - fit::result<new_error_type, value_type>
// - fit::success<value_type>
// - fit::error<new_error_type>
// - fasync::pending
// - fasync::try_ready<new_error_type, value_type>
// - fasync::try_poll<new_error_type, value_type>
// - fasync::try_future<new_error_type, value_type>
// - any callable or unboxed future with the following signature:
//   fit::result<new_error_type, value_type>(fasync::context&)
//
// The handler must accept one of the following argument lists:
// - See above documentation at HANDLER TYPES and substitute T with error_type.
//
// Call pattern:
// - fasync::or_else(<result returning future>, <handler>)
// - <result returning future> | fasync::or_else(<handler>)
//
// This method consumes the future's continuation, leaving it empty.
//
// EXAMPLE
//
//     auto f = fasync::make_future(...) |
//         fasync::or_else([] (const std::string& error) {
//             printf("received error: %s\n", error.c_str());
//             return fit::error();
//         }) |
//         fasync::then(...);
//
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::or_else_combinator or_else;

namespace internal {

class join_closure final : public future_adaptor_closure<join_closure> {
 public:
  template <typename... Fs, requires_conditions<is_future<Fs>...> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(Fs&&... futures) const {
    return join_future_t<Fs...>(std::forward<Fs>(futures)...);
  }

  template <typename A, requires_conditions<::fasync::internal::is_future_applicable<A>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(A&& applicable) const {
    return cpp17::apply(
        [this](auto&&... futures) {
          return this->operator()(std::forward<decltype(futures)>(futures)...);
        },
        std::forward<A>(applicable));
  }

  // TODO(schottm): does not work with void futures (same with below)
  template <template <typename, typename> class C, typename F,
            template <typename> class Allocator = std::allocator,
            requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(C<F, Allocator<F>>&& container) const {
    return join_container_future<C, F, Allocator>(std::move(container));
  }

  // This takes care of both std::array<T, N> and std::span<T, N> where N != std::dynamic_extent
  template <
      template <typename, size_t> class View, typename F, size_t N,
      requires_conditions<is_future<F>, cpp17::bool_constant<N != cpp20::dynamic_extent>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(View<F, N> view) const {
    return join_view_future<View, F, N>(std::move(view));
  }

  template <typename F, requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(cpp20::span<F> span) const {
    return join_span_future<F>(span);
  }

  // Rvalue references to arrays are uncommon but here it forces the user to use std::move() to
  // indicate that the futures are being modified and their outputs will be stored elsewhere.
  template <typename F, size_t N>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F (&&arr)[N]) const {
    return operator()(cpp20::span<F, N>(arr));
  }

  template <typename F, requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F&& future) const {
    return then(std::forward<F>(future), [this](auto&& futures) {
      return this->operator()(std::forward<decltype(futures)>(futures));
    });
  }
};

}  // namespace internal

// |fasync::join|
//
// Can join several futures into one future in several different ways, and this new future does not
// complete until all of the constituent futures complete. The futures can be given as a parameter
// pack, a |std::tuple| or other type usable by |cpp17::apply|, a container like |std::vector|,
// |std::deque|, or |std::list|, a |cpp20::span| or |std::array|, or a bare array.
//
// Call pattern:
// - fasync::join(<futures>...) -> |std::tuple| of outputs
// - fasync::join(std::tuple<futures...>) -> |std::tuple| of outputs
// - fasync::join(<container of futures with |.push_back()|>) -> same container template of outputs
// - fasync::join(std::array<future, N>) -> |std::array| of outputs
// - fasync::join(cpp20::span<future, N>) -> |std::array| of outputs
// - fasync::join(cpp20::span<future>) -> |std::vector| of outputs
// - fasync::join(future (&&)[N]) -> |std::array| of outputs
// - <future of one of the collections of futures above> | fasync::join -> output as above
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::join_closure join;

namespace internal {

template <typename... Fs>
class join_with_closure final : future_adaptor_closure<join_with_closure<Fs...>> {
  static_assert(cpp17::conjunction_v<is_future<Fs>...>, "");

 public:
  explicit constexpr join_with_closure(Fs&&... futures) : futures_(std::forward<Fs>(futures)...) {}

  template <typename F, requires_conditions<is_future<F>> = true>
  constexpr auto operator()(F&& future) {
    return cpp17::apply(
        [&](auto&&... futures) {
          return join(std::forward<F>(future), std::forward<decltype(futures)>(futures)...);
        },
        std::move(futures_));
  }

 private:
  std::tuple<Fs...> futures_;
};

template <typename... Fs>
using join_with_closure_t = join_with_closure<std::decay_t<Fs>...>;

class join_with_combinator final {
 public:
  template <typename... Fs, requires_conditions<is_future<Fs>...> = true>
  LIB_FASYNC_NODISCARD constexpr join_with_closure_t<Fs...> operator()(Fs&&... futures) const {
    return join_with_closure_t<Fs...>(std::forward<Fs>(futures)...);
  }

  template <typename T, requires_conditions<::fasync::internal::is_future_applicable<T>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(T&& applicable) const {
    return with_applicable(
        std::forward<T>(applicable),
        std::make_index_sequence<cpp17::tuple_size_v<cpp20::remove_cvref_t<T>>>());
  }

 private:
  template <typename T, size_t... Is,
            requires_conditions<::fasync::internal::is_future_applicable<T>> = true>
  constexpr auto with_applicable(T&& applicable, std::index_sequence<Is...>) const {
    return join_with_closure_t<std::tuple_element_t<Is, T>...>(std::get<Is>(applicable)...);
  }
};

}  // namespace internal

// |fasync::join_with|
//
// Like |fasync::join| except that its first constituent future can be from a pipeline, so it does
// not need to appear at the start of a pipeline.
//
// Call pattern:
// - <future> | fasync::join_with(<futures>...)
// - <future> | fasync::join_with(std::tuple<futures...>)
// - <future> | fasync::join_with(std::array<future, N>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::join_with_combinator join_with;

// |fasync::wrap|
//
// Allows futures to be wrapped with wrapper objects, which take futures and can introduce arbitrary
// behavior onto them, like |fasync::scope| and |fasync::sequencer|.
//
// Call pattern:
// - fasync::wrap(<future>, <wrapper>, <args>...)
//
// TODO(schottm): merge wrap and wrap_with?
template <typename F, typename W, typename... Args,
          ::fasync::internal::requires_conditions<is_future<F>> = true>
LIB_FASYNC_NODISCARD constexpr auto wrap(F&& future, W& wrapper, Args&&... args) {
  return wrapper.wrap(std::forward<F>(future), std::forward<Args>(args)...);
}

namespace internal {

template <typename W, typename... Args>
class wrap_with_closure final : future_adaptor_closure<wrap_with_closure<W, Args...>> {
 public:
  explicit constexpr wrap_with_closure(W& wrapper, Args&&... args)
      : wrapper_(wrapper), args_(std::forward<Args>(args)...) {}

  template <typename F, requires_conditions<is_future<F>> = true>
  constexpr auto operator()(F&& future) const {
    return cpp17::apply(
        [&](auto&&... args) {
          // TODO(schottm): ADL?
          return wrap(std::forward<F>(future), wrapper_, std::forward<decltype(args)>(args)...);
        },
        std::move(args_));
  }

 private:
  W& wrapper_;
  std::tuple<Args...> args_;
};

template <typename W, typename... Args>
using wrap_with_closure_t = wrap_with_closure<std::decay_t<W>, std::decay_t<Args>...>;

class wrap_with_combinator final {
 public:
  template <typename W, typename... Args>
  constexpr wrap_with_closure_t<W, Args...> operator()(W& wrapper, Args&&... args) const {
    return wrap_with_closure_t<W, Args...>(wrapper, std::forward<Args>(args)...);
  }
};

}  // namespace internal

// |fasync::wrap_with|
//
// Allows futures to be wrapped with wrapper objects, which take futures and can introduce arbitrary
// behavior onto them, like |fasync::scope| and |fasync::sequencer|.
//
// Call pattern:
// - <future> | fasync::wrap_with(<wrapper>, <args>...)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::wrap_with_combinator wrap_with;

namespace internal {

class box_closure final : public future_adaptor_closure<box_closure> {
 public:
  template <typename F, requires_conditions<is_future<F>> = true>
  LIB_FASYNC_NODISCARD constexpr auto operator()(F&& future) const {
    return ::fasync::future<future_output_t<F>>(std::forward<F>(future));
  }
};

}  // namespace internal

// |fasync::box|
//
// Takes an unboxed future and puts its continuation in a |fit::function|, boxing it. This always
// happens before a future is executed on an executor, though not necessarily by this combinator.
// This combinator can be useful when making collections of futures with the same return type.
//
// Call pattern:
// - fasync::box(<future>)
// - <future> | fasync::box
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::box_closure box;

// TODO(schottm): make niebloid with full ADL compatibility for executors with different run methods
template <typename E>
constexpr void run(E& executor) {
  executor.run();
}

namespace internal {

template <typename E, requires_conditions<is_executor<E>> = true>
class block_on_closure final : future_adaptor_closure<block_on_closure<E>> {
 public:
  template <typename F, requires_conditions<std::is_constructible<E&, F&>> = true>
  explicit constexpr block_on_closure(F& executor) : executor_(executor) {}

  template <typename F, requires_conditions<is_future<F>> = true>
  constexpr cpp17::optional<future_output_t<F>> operator()(F&& future) const {
    return block_on(std::forward<F>(future), executor_);
  }

 private:
  E& executor_;
};

class block_on_combinator final {
 public:
  template <typename F, typename E,
            requires_conditions<is_future<F>, cpp17::negation<is_void_future<F>>> = true>
  constexpr cpp17::optional<future_output_t<F>> operator()(F&& future, E& executor) const {
    cpp17::optional<future_output_t<F>> output;
    executor.schedule(std::forward<F>(future) | then([&output](future_output_t<F>& result) {
                        move_construct_optional(output, std::move(result));
                      }));
    using ::fasync::run;
    run(executor);
    return output;
  }

  template <typename F, typename E, requires_conditions<is_void_future<F>> = true>
  constexpr bool operator()(F&& future, E& executor) const {
    bool done = false;
    executor.schedule(std::forward<F>(future) | then([&done] { done = true; }));
    using ::fasync::run;
    run(executor);
    return done;
  }

  template <typename E, requires_conditions<is_executor<E>> = true>
  LIB_FASYNC_NODISCARD constexpr block_on_closure<E> operator()(E& executor) {
    return block_on_closure<E>(executor);
  }
};

}  // namespace internal

// |fasync::block_on|
//
// Executes a future on the current thread using the given executor. The executor should have a
// |run()| function findable via ADL or a |.run()| method otherwise. This combinator returns a
// |cpp17::optional| or a |bool| (for |void|-returning futures) to indicate whether the future was
// abandoned.
//
// Call pattern:
// - fasync::block_on(<future>, <executor>)
// - <future> | fasync::block_on(<executor>)
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::block_on_combinator block_on;

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_FUTURE_H_
