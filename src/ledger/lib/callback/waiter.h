// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_WAITER_H_
#define SRC_LEDGER_LIB_CALLBACK_WAITER_H_

#include <lib/fit/function.h>

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "src/ledger/lib/callback/scoped_callback.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_counted.h"
#include "src/ledger/lib/memory/ref_ptr.h"

namespace ledger {

namespace internal {

template <typename... T>
class ResultAccumulatorValue {
 public:
  using Value = std::tuple<T...>;

  template <typename... T1>
  static Value Build(T1&&... args) {
    return std::make_tuple(std::forward<T1>(args)...);
  }
};

template <typename T>
class ResultAccumulatorValue<T> {
 public:
  using Value = T;

  static Value Build(T arg) { return arg; }
};

template <typename S, typename... T>
class ResultAccumulator {
 public:
  using Value = typename ResultAccumulatorValue<T...>::Value;

  explicit ResultAccumulator(S success_status)
      : success_status_(success_status), result_status_(success_status_) {}

  size_t PrepareCall() {
    results_.emplace_back();
    return results_.size() - 1;
  }

  template <typename... T1>
  bool Update(size_t index, S status, T1&&... args) {
    if (status != success_status_) {
      result_status_ = status;
      results_.clear();
      return false;
    }
    results_[index] = ResultAccumulatorValue<T...>::Build(std::forward<T1>(args)...);
    return true;
  }

  std::pair<S, std::vector<Value>> Result() {
    return std::make_pair(result_status_, std::move(results_));
  }

 private:
  size_t current_index_ = 0;
  std::vector<Value> results_;
  S success_status_;
  S result_status_;
};

template <typename S>
class StatusAccumulator {
 public:
  explicit StatusAccumulator(S success_status)
      : success_status_(success_status), result_status_(success_status_) {}

  bool PrepareCall() { return true; }

  bool Update(bool /*token*/, S status) {
    result_status_ = status;
    return success_status_ == result_status_;
  }

  S Result() { return result_status_; }

 private:
  S success_status_;
  S result_status_;
};

// AnyAccumulator is the accumulator for the AnyWaiter class.
// It continues until an |Update| call matches |success_status|.
template <typename S, typename V>
class AnyAccumulator {
 public:
  AnyAccumulator(S success_status, S default_status, V default_value)
      : success_status_(success_status),
        result_status_(default_status),
        value_(std::move(default_value)) {}

  bool PrepareCall() { return true; }

  bool Update(bool /*token*/, S status, V value) {
    if (status == success_status_) {
      value_ = std::move(value);
    }
    result_status_ = std::move(status);
    // Continue until we get a success.
    return result_status_ != success_status_;
  }

  std::pair<S, V> Result() { return std::make_pair(result_status_, std::move(value_)); }

 private:
  const S success_status_;
  S result_status_;
  V value_;
};

template <typename S, typename V>
class PromiseAccumulator {
 public:
  PromiseAccumulator(S default_status, V default_value)
      : status_(default_status), value_(std::move(default_value)) {}

  bool PrepareCall() { return true; }

  bool Update(bool /*token*/, S status, V value) {
    status_ = std::move(status);
    value_ = std::move(value);
    return false;
  }

  std::pair<S, V> Result() { return std::make_pair(status_, std::move(value_)); }

 private:
  S status_;
  V value_;
};

class CompletionAccumulator {
 public:
  bool PrepareCall() { return true; }

  bool Update(bool /*token*/) { return true; }

  bool Result() { return true; }
};

// Implements operator bool() for a waiter, for use in |MakeScoped|.
template <typename W>
class WaiterWitness {
 public:
  explicit WaiterWitness(RefPtr<W> waiter) : waiter_(waiter) {}

  // Returns |true| if the waiter is in state |STARTED|.
  explicit operator bool() { return waiter_->state_ == W::State::STARTED; }

 private:
  RefPtr<W> waiter_;
};

}  // namespace internal

// Base implementation for all specialized waiters.
//
// A waiter is in one of the following states:
// - STARTED: initial state. Creates new waiting callbacks, and accumulates
// their results (see |Accumulator| below). Moves to FINISHED if the waiter is
// finalized and all callbacks have completed successfully, as reported by
// |Accumulator::Update|. Moves to DONE immediately if one of the waiting
// callbacks fails. Moves to CANCELLED immediately if the waiter is cancelled.
// - DONE: ignores all future waiting callback completions. Waits until the
// waiter is either finalized or cancelled, then moves to FINISHED or CANCELLED
// respectively.
// - CANCELLED: ignores all future waiting callback completions, never calls the
// finalization callback.
// - FINISHED: calls the finalization callback with the accumulated result of
// all unignored waiting callbacks. Ignores all future waiting callback
// completions.
//
// The base implementation is specialized through an Accumulator abstraction to
// aggregate results from the different callbacks. |A| is the accumulator, |R|
// is the final return type and |Args| are the arguments of the callbacks. The
// accumulator must have the following interface:
// class Accumulator {
//  public:
//   // Called once upon creation of each waiting callback. Returns a TOKEN
//   // passed to Update() with the result of the call.
//   TOKEN PrepareCall();
//   // Called once upon completion of each waiting callback. Returns true on
//   // success, false on failure. In case of failure, the waiter is done
//   // immediately and will ignore subsequent waiting callbacks.
//   bool Update(TOKEN token, Args... args);
//   // Returns the result of the aggregation, passed to the finalization
//   // callback of the waiter.
//   R Result();
// };
template <typename A, typename R, typename... Args>
class BaseWaiter : public RefCountedThreadSafe<BaseWaiter<A, R, Args...>> {
 public:
  // Returns a callback for the waiter to wait on. This method must not be
  // called once |Finalize| or |Cancel| have been called.
  // If the waiter is done already when |NewCallback| is called, the callback is
  // a no-op. If the waiter is not done, the callback will pass its parameters
  // to the accumulator (unless the waiter has become done in the meantime
  // because one of the waiting callbacks failed).
  fit::function<void(Args...)> NewCallback() {
    LEDGER_DCHECK(!result_callback_) << "Waiter was already finalized.";
    LEDGER_DCHECK(state_ != State::CANCELLED) << "Waiter has been cancelled.";
    if (state_ != State::STARTED) {
      return [](Args...) {};
    }
    ++pending_callbacks_;
    return [waiter_ref = RefPtr<BaseWaiter<A, R, Args...>>(this),
            token = accumulator_.PrepareCall()](Args&&... args) mutable {
      LEDGER_DCHECK(waiter_ref) << "Callbacks returned by a Waiter must be called only once.";
      // Moving ref to the stack to ensure that the callback is not called
      // inside the finalize callback.
      auto ref = std::move(waiter_ref);
      waiter_ref = nullptr;
      ref->ReturnResult(std::move(token), std::forward<Args>(args)...);
    };
  }

  // Finalizes the waiter. Must be called at most once. The |callback| must be
  // valid until called or until the waiter is cancelled.
  void Finalize(fit::function<void(R)> callback) {
    if (state_ == State::CANCELLED) {
      return;
    }
    // This is a programmer error.
    LEDGER_DCHECK(!result_callback_) << "Waiter already finalized, can't finalize more!";
    // This should never happen: FINISHED can only be reached after having
    // called Finalize, and Finalize can only be called once.
    LEDGER_DCHECK(state_ != State::FINISHED) << "Waiter already finished.";
    result_callback_ = std::move(callback);
    ExecuteCallbackIfFinished();
  }

  // Cancels the waiter.
  void Cancel() {
    if (state_ == State::FINISHED) {
      return;
    }
    state_ = State::CANCELLED;
    // Ensure the callback is not retained.
    result_callback_ = nullptr;
  }

  // Scopes a callback to this waiter: the callback is only called if the waiter is active.  This
  // implies that the finalizer is still alive, so callbacks can use objects owned by the finalizer.
  template <typename Callback>
  auto MakeScoped(Callback callback) {
    return ::ledger::MakeScoped(internal::WaiterWitness(RefPtr<BaseWaiter<A, R, Args...>>(this)),
                                std::move(callback));
  }

 protected:
  explicit BaseWaiter(A&& accumulator) : accumulator_(std::move(accumulator)) {}
  virtual ~BaseWaiter() {}

 private:
  // The waiter state. See class comment for allowed transitions.
  enum class State { STARTED, DONE, CANCELLED, FINISHED };

  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(BaseWaiter);
  LEDGER_FRIEND_MAKE_REF_COUNTED(BaseWaiter);
  friend class internal::WaiterWitness<BaseWaiter<A, R, Args...>>;

  // Receives the result of a |NewCallback| callback and accumulates it if not
  // already done, cancelled or finished. Then executes the finalization
  // callback if necessary.
  template <typename T>
  void ReturnResult(T token, Args... args) {
    LEDGER_DCHECK(pending_callbacks_ > 0);
    --pending_callbacks_;
    if (state_ != State::STARTED) {
      return;
    }
    const bool success = accumulator_.Update(std::move(token), std::forward<Args>(args)...);
    if (!success) {
      state_ = State::DONE;
    }
    ExecuteCallbackIfFinished();
  }

  // Executes the finalization callback if the waiter is finalized, and there
  // are no more pending callbacks or the waiter is done.
  // Must only be called in STARTED or DONE state.
  void ExecuteCallbackIfFinished() {
    LEDGER_DCHECK(state_ != State::FINISHED) << "Waiter already finished.";
    LEDGER_DCHECK(state_ != State::CANCELLED)
        << "Cancelled waiter tried to execute the finalization callback.";
    if (!result_callback_ || (state_ == State::STARTED && pending_callbacks_ > 0)) {
      return;
    }
    state_ = State::FINISHED;
    // Ensure the callback does not live after finalization. Since it might
    // delete this class, we move it to the stack first.
    auto result_callback = std::move(result_callback_);
    result_callback_ = nullptr;
    result_callback(accumulator_.Result());
    return;
  }

  A accumulator_;
  State state_ = State::STARTED;
  // Number of callbacks returned by NewCallback() that have not yet completed.
  size_t pending_callbacks_ = 0;
  // Finalization callback. Must be set before moving to state FINISHED.  Must
  // be unset in states CANCELLED and FINISHED: we should not retain callbacks
  // that will not be called.
  fit::function<void(R)> result_callback_;
};

// Waiter can be used to collate the results of many asynchronous calls into one
// callback. A typical usage example would be:
// auto waiter = MakeRefCounted<callback::Waiter<Status,
//                                   std::unique_ptr<Object>>>(Status::OK);
// storage->GetObject(object_digest1, waiter->NewCallback());
// storage->GetObject(object_digest2, waiter->NewCallback());
// storage->GetObject(object_digest3, waiter->NewCallback());
// ...
// waiter->Finalize([](Status s, std::vector<std::unique_ptr<Object>> v) {
//   do something with the returned objects
// });
//
// If the callbacks have multiple argument in sus of Status, the result are
// accumulated in a std::vector of std::tuples.
template <class S, class... T>
class Waiter : public BaseWaiter<
                   internal::ResultAccumulator<S, T...>,
                   std::pair<S, std::vector<typename internal::ResultAccumulator<S, T...>::Value>>,
                   S, T...> {
 public:
  using Value = typename internal::ResultAccumulator<S, T...>::Value;
  void Finalize(fit::function<void(S, std::vector<Value>)> callback) {
    BaseWaiter<internal::ResultAccumulator<S, T...>, std::pair<S, std::vector<Value>>, S,
               T...>::Finalize([callback =
                                    std::move(callback)](std::pair<S, std::vector<Value>> result) {
      callback(result.first, std::move(result.second));
    });
  }

 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(Waiter);
  LEDGER_FRIEND_MAKE_REF_COUNTED(Waiter);
  ~Waiter() override{};

  explicit Waiter(S success_status)
      : BaseWaiter<internal::ResultAccumulator<S, T...>, std::pair<S, std::vector<Value>>, S, T...>(
            internal::ResultAccumulator<S, T...>(success_status)) {}
};

// StatusWaiter can be used to collate the results of many asynchronous calls
// into one callback. It is different from Waiter in that the callbacks only use
// S (e.g. Status) as an argument.
template <class S>
class StatusWaiter : public BaseWaiter<internal::StatusAccumulator<S>, S, S> {
 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(StatusWaiter);
  LEDGER_FRIEND_MAKE_REF_COUNTED(StatusWaiter);

  explicit StatusWaiter(S success_status)
      : BaseWaiter<internal::StatusAccumulator<S>, S, S>(
            internal::StatusAccumulator<S>(success_status)) {}
  ~StatusWaiter() override{};
};

// AnyWaiter is used to wait many asynchronous calls and returns the first
// successful result. It will return |default_status| and |default_value| only
// if no callback was called with a |success_status| status.
template <class S, class V>
class AnyWaiter : public BaseWaiter<internal::AnyAccumulator<S, V>, std::pair<S, V>, S, V> {
 public:
  void Finalize(fit::function<void(S, V)> callback) {
    BaseWaiter<internal::AnyAccumulator<S, V>, std::pair<S, V>, S, V>::Finalize(
        [callback = std::move(callback)](std::pair<S, V> result) {
          callback(result.first, std::move(result.second));
        });
  }

 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(AnyWaiter);
  LEDGER_FRIEND_MAKE_REF_COUNTED(AnyWaiter);

  // Creates a new waiter. |success_status| and |default_value| will be
  // returned to the callback in |Finalize| if |NewCallback| is not called.
  AnyWaiter(S success_status, S default_status, V default_value = V())
      : BaseWaiter<internal::AnyAccumulator<S, V>, std::pair<S, V>, S, V>(
            internal::AnyAccumulator<S, V>(success_status, default_status,
                                           std::move(default_value))) {}
  ~AnyWaiter() override{};
};

// Promise is used to wait on a single asynchronous call. A typical usage
// example is:
// auto promise =
//     MakeRefCounted<Promise<Status, std::unique_ptr<Object>>>(
//         Status::ILLEGAL_STATE);
// storage->GetObject(object_digest1, promise->NewCallback());
// ...
//
// promise->Finalize([](Status s, std::unique_ptr<Object> o) {
//   do something with the returned object
// });
template <class S, class V>
class Promise : public BaseWaiter<internal::PromiseAccumulator<S, V>, std::pair<S, V>, S, V> {
 public:
  void Finalize(fit::function<void(S, V)> callback) {
    BaseWaiter<internal::PromiseAccumulator<S, V>, std::pair<S, V>, S, V>::Finalize(
        [callback = std::move(callback)](std::pair<S, V> result) {
          callback(result.first, std::move(result.second));
        });
  }

 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(Promise);
  LEDGER_FRIEND_MAKE_REF_COUNTED(Promise);

  // Creates a new promise. |default_status| and |default_value| will be
  // returned to the callback in |Finalize| if |NewCallback| is not called.
  Promise(S default_status, V default_value = V())
      : BaseWaiter<internal::PromiseAccumulator<S, V>, std::pair<S, V>, S, V>(
            internal::PromiseAccumulator<S, V>(default_status, std::move(default_value))) {}
  ~Promise() override{};
};

// CompletionWaiter can be used to be notified on completion of a computation.
class CompletionWaiter : public BaseWaiter<internal::CompletionAccumulator, bool> {
 public:
  void Finalize(fit::function<void()> callback) {
    BaseWaiter<internal::CompletionAccumulator, bool>::Finalize(
        [callback = std::move(callback)](bool result) { callback(); });
  }

 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(CompletionWaiter);
  LEDGER_FRIEND_MAKE_REF_COUNTED(CompletionWaiter);

  CompletionWaiter()
      : BaseWaiter<internal::CompletionAccumulator, bool>(internal::CompletionAccumulator()) {}
  ~CompletionWaiter() override{};
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_WAITER_H_
