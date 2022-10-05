// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BRIDGE_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BRIDGE_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/internal/bridge.h>

namespace fasync {

// |fasync::bridge|
//
// A bridge is a building block for asynchronous control flow that is formed by the association of
// two distinct participants: a completer and a consumer.
//
// - The completer is responsible for reporting completion of an asynchronous task and providing
//   its result. See |completer| and |fasync::completer|.
// - The consumer is responsible for consuming the result of the asynchronous task. See |consumer|
//   and |fasync::consumer|.
//
// This class is often used for binding a |fasync::future| to a callback, facilitating
// interoperation of futures with functions that asynchronously report their result via a callback
// function. It can also be used more generally anytime it is necessary to decouple completion of
// an asynchronous task from consumption of its result (possibly on different threads).
//
// The completer and consumer each possesses a unique capability that can be exercised at most once
// during their association: the asynchronous task represented by a bridge can be completed at most
// once and its result can be consumed at most once. This property is enforced by a single-ownership
// model for completers and consumers.
//
// The completion capability has a single owner represented by |fasync::completer|.  Its owner may
// exercise the capability to complete the task (provide its result), it may transfer the capability
// by moving it to another completer instance, or it may cause the asynchronous task to be
// "abandoned" by discarding the capability, implying that the task can never produce a result. When
// this occurs, the associated consumer's |fasync::consumer::was_abandoned()| method will return
// true and the consumer will not obtain any result from the task. See |fasync::consumer::future()|
// and |fasync::consumer::future_or()| for details on how abandonment of the task can be handled by
// the consumer.
//
// The consumption capability has a single owner represented by |fasync::consumer|. Its owner may
// exercise the capability to consume the task's result (as a future), it may transfer the
// capability by moving it to another consumer instance, or it may cause the asynchronous task to
// be "canceled" by discarding the capability, implying that the task's result can never be
// consumed. When this occurs, the associated completer's |fasync::completer::was_canceled()| method
// will return true and the task's eventual result (if any) will be silently discarded.
//
// DECOUPLING
//
// See |fasync::schedule_for_consumer| and |fasync::split| for a helper which uses a bridge to
// decouple completion and consumption of a task's result so they can be performed on different
// executors.
//
// SYNOPSIS
//
// |E| is the type of error produced when the task completes with an error.
//
// |Ts...| is the type of value produced when the task completes successfully. Use
// |std::tuple<Args...>| if the task produces multiple values, such as when you intend to bind the
// task's completer to a callback with multiple arguments using |fasync::completer::bind()|.
//
// EXAMPLE
//
// Imagine a File I/O library offers a callback-based asynchronous reading function. We suppose that
// the read handling code will invoke the callback upon completion. The library's API might look a
// bit like this:
//
//     using read_callback = fit::function<void(size_t bytes_read)>;
//     void read_async(size_t num_bytes, uint8_t* buffer, read_callback cb);
//
// Here's how we can adapt the library's |read_async| function to a |fasync::future| by binding its
// callback to a bridge:
//
//     fasync::try_future<fit::failed, size_t> future_read(uint8_t* buffer, size_t num_bytes) {
//       fasync::bridge<fit::failed, size_t> bridge;
//       read_async(num_bytes, buffer, bridge.completer.bind());
//       return bridge.consumer.future_or(fit::failed());
//     }
//
// Finally we can chain additional asynchronous tasks to be performed upon completion of the
// promised read:
//
//     auto buffer = std:make_unique<uint8_t[]>(4096);
//     void my_program(fasync::executor& executor) {
//       auto future = future_read(buffer.get(), sizeof(buffer)) |
//           fasync::and_then([buffer = std::move(buffer)](const size_t& bytes_read) {
//               // Consume contents of buffer.
//           }) |
//           fasync::or_else([] {
//               // Handle error case.
//           });
//       executor.schedule(std::move(future));
//     }
//
// Similarly, suppose the File I/O library offers a callback-based asynchronous writing function
// that can return a variety of errors encoded as negative sizes. Here's how we might decode those
// errors uniformly into |fit::result| allowing them to be handled using combinators such as
// |fasync::or_else|.
//
//     using write_callback = fit::function<void(size_t bytes_written, int error)>;
//     void write_async(size_t num_bytes, uint8_t* buffer, write_callback cb);
//
//     fasync::try_future<int, size_t> future_write(uint8_t* buffer, size_t num_bytes) {
//       fasync::bridge<int, size_t> bridge;
//       write_async(num_bytes, buffer,
//           [completer = std::move(bridge.completer)](size_t bytes_written, int error) {
//           if (bytes_written == 0) {
//               completer.complete_error(error);
//               return;
//           }
//           completer.complete_ok(bytes_written);
//       });
//       return bridge.consumer.future_or(fit::error(ERR_ABANDONED));
//     }
//
//     auto buffer == std::make_unique<uint8_t[]>(4096);
//     void my_program(fasync::executor& executor) {
//       auto future = future_write(buffer.get(), sizeof(buffer)) |
//           fasync::and_then([buffer = std::move(buffer)](const size_t& bytes_written) {
//               // Consume contents of buffer.
//           }) |
//           fasync::or_else([](const int& error) {
//               // Handle error case.
//           });
//       executor.schedule(std::move(future));
//     }
//
// See documentation of |fasync::future| for more information.
template <typename E = ::fit::failed, typename... Ts>
class bridge final {
 public:
  using result_type = ::fit::result<E, Ts...>;
  using completer_type = ::fasync::completer<E, Ts...>;
  using consumer_type = ::fasync::consumer<E, Ts...>;

  // Creates a bridge representing a new asynchronous task formed by the association of a completer
  // and consumer.
  bridge() {
    ::fasync::internal::bridge_state<E, Ts...>::create(completer.completion_ref_,
                                                       consumer.consumption_ref_);
  }
  bridge(const bridge& other) = delete;
  bridge& operator=(const bridge& other) = delete;
  bridge(bridge&& other) = default;
  bridge& operator=(bridge&& other) = default;

  ~bridge() = default;

  // The bridge's completer capability.
  completer_type completer;

  // The bridge's consumer capability.
  consumer_type consumer;
};

// |fasync::completer|
//
// Provides a result upon completion of an asynchronous task.
//
// Instances of this class have single-ownership of a unique capability for completing the task.
// This capability can be exercised at most once. Ownership of the capability is implicitly
// transferred away when the completer is abandoned, completed, or bound to a callback.
//
// See also |fasync::bridge|.
// See documentation of |fasync::future| for more information.
//
// SYNOPSIS
//
// |E| is the type of error produced when the task completes with an error.
//
// |Ts...| is the type of value produced when the task completes successfully. Use
// |std::tuple<Args...>| if the task produces multiple values, such as when you intend to bind the
// task's completer to a callback with multiple arguments using |fasync::completer::bind()|.
template <typename E, typename... Ts>
class completer final {
  using bridge_state = ::fasync::internal::bridge_state<E, Ts...>;
  using completion_ref = typename bridge_state::completion_ref;

 public:
  using result_type = ::fit::result<E, Ts...>;

  completer() = default;

  completer(const completer& other) = delete;
  completer& operator=(const completer& other) = delete;
  completer(completer&& other) = default;
  completer& operator=(completer&& other) = default;

  ~completer() = default;

  // Returns true if this instance currently owns the unique capability for reporting completion of
  // the task.
  explicit operator bool() const { return !!completion_ref_; }

  // Returns true if the associated |consumer| has canceled the task. This method returns a snapshot
  // of the current cancellation state. Note that the task may be canceled concurrently at any time.
  bool was_canceled() const {
    assert(completion_ref_);
    return completion_ref_.get().was_canceled();
  }

  // Explicitly abandons the task, meaning that it will never be completed. See |fasync::bridge| for
  // details about abandonment.
  void abandon() {
    assert(completion_ref_);
    completion_ref_ = completion_ref();
  }

  // Reports that the task has completed successfully. This method takes no arguments if
  // |Ts...| is empty, otherwise it takes one argument which must be assignable to |T|.
  template <typename TT = ::fasync::internal::first<Ts...>,
            ::fasync::internal::requires_conditions<
                cpp17::negation<::fasync::internal::has_type<TT>>> = true>
  void complete_ok() {
    assert(completion_ref_);
    bridge_state& state = completion_ref_.get();
    state.complete(std::move(completion_ref_), ::fit::ok());
  }

  template <
      typename TT = ::fasync::internal::first<Ts...>, typename T = typename TT::type,
      typename R = result_type,
      ::fasync::internal::requires_conditions<std::is_constructible<result_value_t<R>, T>> = true>
  void complete_ok(T&& value) {
    assert(completion_ref_);
    bridge_state& state = completion_ref_.get();
    state.complete(std::move(completion_ref_), ::fit::ok(std::forward<T>(value)));
  }

  // Reports that the task has completed with an error. This method takes no arguments if |E| is
  // |fit::failed|, otherwise it takes one argument which must be assignable to |E|.
  template <typename EE = E,
            ::fasync::internal::requires_conditions<std::is_same<EE, ::fit::failed>> = true>
  void complete_error() {
    assert(completion_ref_);
    bridge_state& state = completion_ref_.get();
    state.complete(std::move(completion_ref_), ::fit::failed());
  }

  template <typename EE = E, ::fasync::internal::requires_conditions<
                                 std::is_constructible<result_error_t<result_type>, EE>> = true>
  void complete_error(EE&& error) {
    assert(completion_ref_);
    bridge_state& state = completion_ref_.get();
    state.complete(std::move(completion_ref_), ::fit::as_error(std::forward<EE>(error)));
  }

  // Reports that the task has completed or been abandoned.
  // See |fasync::bridge| for details about abandonment.
  //
  // The result state determines the task's final disposition.
  // - |fit::success|: The task completed successfully.
  // - |fit::error|: The task completed with an error.
  template <typename R,
            ::fasync::internal::requires_conditions<std::is_constructible<result_type, R>> = true>
  void complete(R&& result) {
    assert(completion_ref_);
    bridge_state& state = completion_ref_.get();
    state.complete(std::move(completion_ref_), std::forward<R>(result));
  }

  // Returns a callback that reports completion of the asynchronous task along with its result when
  // invoked. This method is typically used to bind completion of a task to a callback that has
  // zero, one or more arguments.
  //
  // If |Ts...| is empty, the returned callback's signature is: |void(void)|. Otherwise, the
  // returned callback's signature is: |void(T)| unless |T| is a |std::tuple|. Given a |T| of
  // |std::tuple<Args...>|, the returned callback's signatures is: |void(Args...)|. Note that the
  // tuple's fields are unpacked as individual arguments of the callback.
  //
  // The returned callback is thread-safe and move-only.
  ::fasync::internal::bridge_bind_callback<E, Ts...> bind() {
    assert(completion_ref_);
    return ::fasync::internal::bridge_bind_callback<E, Ts...>(std::move(completion_ref_));
  }

 private:
  friend class bridge<E, Ts...>;

  completion_ref completion_ref_;
};

// |fasync::consumer|
//
// Consumes the result of an asynchronous task.
//
// Instances of this class have single-ownership of a unique capability for consuming the task's
// result. This capability can be exercised at most once. Ownership of the capability is implicitly
// transferred away when the task is canceled or converted to a future.
//
// See also |fasync::bridge|.
// See documentation of |fasync::future| for more information.
//
// SYNOPSIS
//
// |E| is the type of error produced when the task completes with an error.
//
// |Ts...| is the type of value produced when the task completes successfully. Use
// |std::tuple<Args...>| if the task produces multiple values, such as when you intend to bind the
// task's completer to a callback with multiple arguments using |fasync::completer::bind()|.
template <typename E, typename... Ts>
class consumer final {
  using bridge_state = ::fasync::internal::bridge_state<E, Ts...>;
  using consumption_ref = typename bridge_state::consumption_ref;

 public:
  using result_type = ::fit::result<E, Ts...>;

  consumer() = default;

  consumer(const consumer& other) = delete;
  consumer& operator=(const consumer& other) = delete;
  consumer(consumer&& other) = default;
  consumer& operator=(consumer&& other) = default;

  ~consumer() = default;

  // Returns true if this instance currently owns the unique capability for consuming the result of
  // the task upon its completion.
  explicit operator bool() const { return !!consumption_ref_; }

  // Explicitly cancels the task, meaning that its result will never be consumed.
  // See |fasync::bridge| for details about cancellation.
  void cancel() {
    assert(consumption_ref_);
    consumption_ref_ = consumption_ref();
  }

  // Returns true if the associated |completer| has abandoned the task.
  // This method returns a snapshot of the current abandonment state.
  // Note that the task may be abandoned concurrently at any time.
  bool was_abandoned() const {
    assert(consumption_ref_);
    return consumption_ref_.get().was_abandoned();
  }

  // Returns an unboxed future which resumes execution once this task has completed. If the task is
  // abandoned by its completer, the future will not produce a result, thereby causing subsequent
  // tasks associated with the future to also be abandoned and eventually destroyed if they cannot
  // make progress without the promised result.
  auto future() {
    assert(consumption_ref_);
    return typename bridge_state::future_continuation(std::move(consumption_ref_));
  }

  // A variant of |future()| that allows a default result to be provided when the task is abandoned
  // by its completer. Typically this is used to cause the future to return an error when the task
  // is abandoned instead of causing subsequent tasks associated with the future to also be
  // abandoned.
  //
  // The state of |result_if_abandoned| determines the future's behavior in case of abandonment.
  //
  // - |fit::success|: Reports a successful result.
  // - |fit::error|: Reports a failure result.
  template <typename R,
            ::fasync::internal::requires_conditions<std::is_constructible<result_type, R>> = true>
  auto future_or(R&& result_if_abandoned) {
    assert(consumption_ref_);
    return typename bridge_state::future_continuation(std::move(consumption_ref_),
                                                      std::forward<R>(result_if_abandoned));
  }

 private:
  friend class bridge<E, Ts...>;

  consumption_ref consumption_ref_;
};

// |fasync::schedule_for_consumer|
//
// Schedules |future| to run on |executor| and returns a |consumer| which receives the result of the
// future upon its completion.
//
// This method has the effect of decoupling the evaluation of a future from the consumption of its
// result such that they can be performed on different executors (possibly on different threads).
//
// |executor| must outlive the execution of the given future.
// |future| must be non-empty.
//
// EXAMPLE
//
// This example shows an object that encapsulates its own executor which it manages independently
// from that of its clients. This enables the object to obtain certain assurances such as a
// guarantee of single-threaded execution for its internal operations even if its clients happen to
// be multi-threaded (or vice-versa as desired).
//
//     // This model has specialized internal threading requirements so it manages its own executor.
//     class model {
//      public:
//       fasync::consumer<fit::failed, int> perform_calculation(int parameter) {
//           return fasync::schedule_for_consumer(executor_,
//               fasync::make_future([parameter] {
//                   // In reality, this would likely be a much more complex expression.
//                   return fit::ok(parameter * parameter);
//               });
//       }
//
//      private:
//       // The model is responsible for initializing and running its own executor (perhaps on its
//       // own thread).
//       fasync::single_threaded_executor executor_;
//     };
//
//     // Asks the model to perform a calculation, awaits a result on the provided executor (which
//     // is different from the one internally used by the model), then prints the result.
//     void print_output(fasync::executor& executor, model& m) {
//       executor.schedule(
//           m.perform_calculation(16)
//               .future_or(fit::failed()) |
//               fasync::and_then([](const int& result) { printf("done: %d\n", result); }) |
//               fasync::or_else([] { puts("failed or abandoned"); }));
//     }
template <typename F, typename E,
          ::fasync::internal::requires_conditions<
              is_try_future<F>, is_executor<E>, ::fasync::internal::is_value_try_future<F>> = true>
consumer<future_error_t<F>, future_value_t<F>> schedule_for_consumer(F&& future, E& executor) {
  bridge<future_error_t<F>, future_value_t<F>> bridge;
  executor.schedule(
      std::forward<F>(future) |
      fasync::then([completer = std::move(bridge.completer)](future_result_t<F>& result) mutable {
        completer.complete(std::move(result));
      }));
  return std::move(bridge.consumer);
}

template <typename F, typename E,
          ::fasync::internal::requires_conditions<
              is_try_future<F>, is_executor<E>,
              cpp17::negation<::fasync::internal::is_value_try_future<F>>> = true>
consumer<future_error_t<F>> schedule_for_consumer(F&& future, E& executor) {
  bridge<future_error_t<F>> bridge;
  executor.schedule(
      std::forward<F>(future) |
      fasync::then([completer = std::move(bridge.completer)](future_result_t<F>& result) mutable {
        completer.complete(std::move(result));
      }));
  return std::move(bridge.consumer);
}

namespace internal {

template <typename E, requires_conditions<is_executor<E>> = true>
class split_closure final : future_adaptor_closure<split_closure<E>> {
 public:
  template <typename F, requires_conditions<std::is_convertible<F&, E&>> = true>
  explicit constexpr split_closure(F& executor) : executor_(executor) {}

  template <typename F, requires_conditions<is_try_future<F>> = true>
  constexpr auto operator()(F&& future) const {
    return schedule_for_consumer(std::forward<F>(future), executor_).future();
  }

 private:
  E& executor_;
};

// We can't use |combinator<split_combinator>| here because its closure uses |std::decay_t| and we
// need |E| to be kept as a reference.
class split_combinator final {
 public:
  template <typename F, typename E, requires_conditions<is_try_future<F>, is_executor<E>> = true>
  constexpr auto operator()(F&& future, E& executor) const {
    return schedule_for_consumer(std::forward<F>(future), executor).future();
  }

  template <typename E, requires_conditions<is_executor<E>> = true>
  LIB_FASYNC_NODISCARD constexpr split_closure<E> operator()(E& executor) const {
    return split_closure<E>(executor);
  }
};
}  // namespace internal

// |fasync::split|
//
// Like |fasync::schedule_for_consumer|, but can be placed in the middle of a pipeline to switch
// execution contexts (or "split" the execution of a single logical piece of work across multiple
// contexts) on the fly. Equivalent to calling
// |fasync::schedule_for_consumer(<future>, <executor>).future()|.
//
// Call pattern:
// - fasync::split(<future>, <executor>) -> <future to continue on another execution context>
// - <future> | fasync::split(<executor>) -> <future to continue on another execution context>
//
// EXAMPLE
//
// Let's reimagine our previous example if the first executor didn't need to be encapsulated in its
// own class:
//
//     fasync::try_future<fit::failed, int> perform_calculation(int parameter) {
//       return fasync::make_future([parameter] {
//                // In reality, this would likely be a much more complex expression.
//                return fit::ok(parameter * parameter);
//              });
//     }
//
//     void print_output(fasync::executor& executor) {
//       fasync::single_threaded_executor single_threaded;
//
//       perform_calculation(16) |
//       fasync::split(single_threaded) |
//       fasync::and_then([](const int& result) { printf("done: %d\n", result); }) |
//       fasync::or_else([] { puts("failed"); }) |
//       fasync::schedule_on(executor);
//     }
LIB_FASYNC_INLINE_CONSTANT constexpr ::fasync::internal::split_combinator split;

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BRIDGE_H_
