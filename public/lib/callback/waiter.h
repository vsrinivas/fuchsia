// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CALLBACK_WAITER_H_
#define LIB_CALLBACK_WAITER_H_

#include <memory>
#include <utility>
#include <vector>

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

namespace callback {

namespace internal {

template <typename S, typename T>
class ResultAccumulator {
 public:
  explicit ResultAccumulator(S success_status)
      : success_status_(success_status), result_status_(success_status_) {}

  size_t PrepareCall() {
    results_.emplace_back();
    return results_.size() - 1;
  }

  bool Update(size_t index, S status, T result) {
    if (status != success_status_) {
      result_status_ = status;
      results_.clear();
      return false;
    }
    results_[index] = std::move(result);
    return true;
  }

  std::pair<S, std::vector<T>> Result() {
    return std::make_pair(result_status_, std::move(results_));
  }

 private:
  size_t current_index_ = 0;
  std::vector<T> results_;
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

  std::pair<S, V> Result() {
    return std::make_pair(result_status_, std::move(value_));
  }

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

  std::pair<S, V> Result() {
    return std::make_pair(status_, std::move(value_));
  }

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

}  // namespace internal

// Base implementation for all specialized waiters. It uses an Accumulator
// abstraction to aggregate results from the different callbacks.
// |A| is the accumulator, |R| is the final return type and |Args| are the
// arguments of the callbacks. The accumulator must have the following
// interface:
// class Accumulator {
//  public:
//   TOKEN PrepareCall();
//   bool Update(TOKEN token, Args... args);
//   R Result();
// };
template <typename A, typename R, typename... Args>
class BaseWaiter : public fxl::RefCountedThreadSafe<BaseWaiter<A, R, Args...>> {
 public:
  fit::function<void(Args...)> NewCallback() {
    FXL_DCHECK(!finalized_) << "Waiter was already finalized.";
    FXL_DCHECK(!cancelled_) << "Waiter has been cancelled.";
    if (done_) {
      return [](Args...) {};
    }
    ++pending_callbacks_;
    return [waiter_ref = fxl::RefPtr<BaseWaiter<A, R, Args...>>(this),
            token = accumulator_.PrepareCall()](Args&&... args) mutable {
      waiter_ref->ReturnResult(std::move(token), std::forward<Args>(args)...);
    };
  }

  void Finalize(fit::function<void(R)> callback) {
    FXL_DCHECK(!finalized_) << "Waiter already finalized, can't finalize more!";
    FXL_DCHECK(!cancelled_) << "Waiter has been cancelled.";
    result_callback_ = std::move(callback);
    finalized_ = true;
    ExecuteCallbackIfFinished();
  }

  void Cancel() { cancelled_ = true; }

 protected:
  explicit BaseWaiter(A&& accumulator) : accumulator_(std::move(accumulator)) {}
  virtual ~BaseWaiter() {}

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(BaseWaiter);
  FRIEND_MAKE_REF_COUNTED(BaseWaiter);

  template <typename T>
  void ReturnResult(T token, Args... args) {
    if (done_) {
      FXL_DCHECK(!pending_callbacks_);
      return;
    }
    done_ = !accumulator_.Update(std::move(token), std::forward<Args>(args)...);
    if (done_) {
      pending_callbacks_ = 0;
    } else {
      --pending_callbacks_;
    }
    ExecuteCallbackIfFinished();
  }

  void ExecuteCallbackIfFinished() {
    FXL_DCHECK(!finished_) << "Waiter already finished.";
    if (!finalized_ || pending_callbacks_) {
      return;
    }
    finished_ = true;
    if (!cancelled_) {
      result_callback_(accumulator_.Result());
      // The callback might delete this class.
      return;
    }
  }

  A accumulator_;
  // TODO(LE-382): Simplify the state machine here.
  bool done_ = false;
  bool finalized_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
  size_t pending_callbacks_ = 0;

  fit::function<void(R)> result_callback_;
};

// Waiter can be used to collate the results of many asynchronous calls into one
// callback. A typical usage example would be:
// auto waiter = fxl::MakeRefCounted<callback::Waiter<Status,
//                                   std::unique_ptr<Object>>>(Status::OK);
// storage->GetObject(object_digest1, waiter->NewCallback());
// storage->GetObject(object_digest2, waiter->NewCallback());
// storage->GetObject(object_digest3, waiter->NewCallback());
// ...
// waiter->Finalize([](Status s, std::vector<std::unique_ptr<Object>> v) {
//   do something with the returned objects
// });
template <class S, class T>
class Waiter : public BaseWaiter<internal::ResultAccumulator<S, T>,
                                 std::pair<S, std::vector<T>>, S, T> {
 public:
  void Finalize(fit::function<void(S, std::vector<T>)> callback) {
    BaseWaiter<internal::ResultAccumulator<S, T>, std::pair<S, std::vector<T>>,
               S, T>::Finalize([callback =
                                    std::move(callback)](
                                   std::pair<S, std::vector<T>> result) {
      callback(result.first, std::move(result.second));
    });
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Waiter);
  FRIEND_MAKE_REF_COUNTED(Waiter);
  ~Waiter() override{};

  explicit Waiter(S success_status)
      : BaseWaiter<internal::ResultAccumulator<S, T>,
                   std::pair<S, std::vector<T>>, S, T>(
            internal::ResultAccumulator<S, T>(success_status)) {}
};

// StatusWaiter can be used to collate the results of many asynchronous calls
// into one callback. It is different from Waiter in that the callbacks only use
// S (e.g. storage::Status) as an argument.
template <class S>
class StatusWaiter : public BaseWaiter<internal::StatusAccumulator<S>, S, S> {
 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(StatusWaiter);
  FRIEND_MAKE_REF_COUNTED(StatusWaiter);

  explicit StatusWaiter(S success_status)
      : BaseWaiter<internal::StatusAccumulator<S>, S, S>(
            internal::StatusAccumulator<S>(success_status)) {}
  ~StatusWaiter() override{};
};

// AnyWaiter is used to wait many asynchronous calls and returns the first
// successful result. It will return |default_status| and |default_value| only
// if no callback was called with a |success_status| status.
template <class S, class V>
class AnyWaiter
    : public BaseWaiter<internal::AnyAccumulator<S, V>, std::pair<S, V>, S, V> {
 public:
  void Finalize(fit::function<void(S, V)> callback) {
    BaseWaiter<internal::AnyAccumulator<S, V>, std::pair<S, V>, S, V>::Finalize(
        [callback = std::move(callback)](std::pair<S, V> result) {
          callback(result.first, std::move(result.second));
        });
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(AnyWaiter);
  FRIEND_MAKE_REF_COUNTED(AnyWaiter);

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
//     fxl::MakeRefCounted<callback::Promise<Status, std::unique_ptr<Object>>>(
//         Status::ILLEGAL_STATE);
// storage->GetObject(object_digest1, promise->NewCallback());
// ...
//
// promise->Finalize([](Status s, std::unique_ptr<Object> o) {
//   do something with the returned object
// });
template <class S, class V>
class Promise : public BaseWaiter<internal::PromiseAccumulator<S, V>,
                                  std::pair<S, V>, S, V> {
 public:
  void Finalize(fit::function<void(S, V)> callback) {
    BaseWaiter<internal::PromiseAccumulator<S, V>, std::pair<S, V>, S,
               V>::Finalize([callback =
                                 std::move(callback)](std::pair<S, V> result) {
      callback(result.first, std::move(result.second));
    });
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Promise);
  FRIEND_MAKE_REF_COUNTED(Promise);

  // Creates a new promise. |default_status| and |default_value| will be
  // returned to the callback in |Finalize| if |NewCallback| is not called.
  Promise(S default_status, V default_value = V())
      : BaseWaiter<internal::PromiseAccumulator<S, V>, std::pair<S, V>, S, V>(
            internal::PromiseAccumulator<S, V>(default_status,
                                               std::move(default_value))) {}
  ~Promise() override{};
};

// CompletionWaiter can be used to be notified on completion of a computation.
class CompletionWaiter
    : public BaseWaiter<internal::CompletionAccumulator, bool> {
 public:
  void Finalize(fit::function<void()> callback) {
    BaseWaiter<internal::CompletionAccumulator, bool>::Finalize(
        [callback = std::move(callback)](bool result) { callback(); });
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CompletionWaiter);
  FRIEND_MAKE_REF_COUNTED(CompletionWaiter);

  CompletionWaiter()
      : BaseWaiter<internal::CompletionAccumulator, bool>(
            internal::CompletionAccumulator()) {}
  ~CompletionWaiter() override{};
};

}  // namespace callback

#endif  // LIB_CALLBACK_WAITER_H_
