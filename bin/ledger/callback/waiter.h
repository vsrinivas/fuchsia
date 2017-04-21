// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_WAITER_H_
#define APPS_LEDGER_SRC_CALLBACK_WAITER_H_

#include <memory>
#include <utility>
#include <vector>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace callback {

namespace internal {

// Base implementation for all specialized waiter. It uses an Accumulator
// abstraction to aggregate results from the different callbacks.
// A is the accumulator, R is the final return type and Args are the arguments
// of the callbacks. The accumulator must have the following interface:
// class Accumulator {
//  public:
//   TOKEN PrepareCall();
//   bool Update(TOKEN token, Args... args);
//   R Result();
// };
template <typename A, typename R, typename... Args>
class BaseWaiter : public ftl::RefCountedThreadSafe<BaseWaiter<A, R, Args...>> {
 public:
  std::function<void(Args...)> NewCallback() {
    FTL_DCHECK(!finalized_);
    if (done_) {
      return [](Args...) {};
    }
    ++pending_callbacks_;
    return [
      waiter_ref = ftl::RefPtr<BaseWaiter<A, R, Args...>>(this),
      token = accumulator_.PrepareCall()
    ](Args && ... args) mutable {
      waiter_ref->ReturnResult(std::move(token), std::forward<Args>(args)...);
    };
  }

  void Finalize(std::function<void(R)> callback) {
    FTL_DCHECK(!finalized_) << "Waiter already finalized, can't finalize more!";
    result_callback_ = std::move(callback);
    finalized_ = true;
    ExecuteCallbackIfFinished();
  }

 protected:
  BaseWaiter(A&& accumulator) : accumulator_(std::move(accumulator)) {}

 private:
  template <typename T>
  void ReturnResult(T token, Args... args) {
    if (done_) {
      FTL_DCHECK(!pending_callbacks_);
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
    FTL_DCHECK(!finished_) << "Waiter already finished.";
    if (finalized_ && !pending_callbacks_) {
      result_callback_(accumulator_.Result());
      finished_ = true;
    }
  }

  A accumulator_;
  bool done_ = false;
  bool finalized_ = false;
  bool finished_ = false;
  size_t pending_callbacks_ = 0;

  std::function<void(R)> result_callback_;
};

template <typename S, typename T>
class ResultAccumulator {
 public:
  ResultAccumulator(S default_status)
      : default_status_(default_status), result_status_(default_status_) {}

  size_t PrepareCall() {
    results_.emplace_back();
    return results_.size() - 1;
  }

  bool Update(size_t index, S status, T result) {
    if (status != default_status_) {
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
  S default_status_;
  S result_status_;
};

template <typename S>
class StatusAccumulator {
 public:
  StatusAccumulator(S default_status)
      : default_status_(default_status), result_status_(default_status_) {}

  bool PrepareCall() { return true; }

  bool Update(bool, S status) {
    result_status_ = status;
    return default_status_ == result_status_;
  }

  S Result() { return result_status_; }

 private:
  S default_status_;
  S result_status_;
};

class CompletionAccumulator {
 public:
  bool PrepareCall() { return true; }

  bool Update(bool) { return true; }

  bool Result() { return true; }
};

}  // namespace internal

// Waiter can be used to collate the results of many asynchronous calls into one
// callback. A typical usage example would be:
// auto waiter = callback::Waiter<Status,
//                                std::unique_ptr<Object>>::Create(Status::OK);
// storage->GetObject(object_id1, waiter.NewCallback());
// storage->GetObject(object_id2, waiter.NewCallback());
// storage->GetObject(object_id3, waiter.NewCallback());
// ...
// waiter->Finalize([](Status s, std::vector<std::unique_ptr<Object>> v) {
//   do something with the returned objects
// });
template <class S, class T>
class Waiter : public internal::BaseWaiter<internal::ResultAccumulator<S, T>,
                                           std::pair<S, std::vector<T>>,
                                           S,
                                           T> {
 public:
  static ftl::RefPtr<Waiter<S, T>> Create(S default_status) {
    return ftl::AdoptRef(new Waiter<S, T>(default_status));
  }

  void Finalize(std::function<void(S, std::vector<T>)> callback) {
    internal::BaseWaiter<internal::ResultAccumulator<S, T>,
                         std::pair<S, std::vector<T>>, S,
                         T>::Finalize([callback =
                                           std::move(callback)](
        std::pair<S, std::vector<T>> result) {
      callback(result.first, std::move(result.second));
    });
  }

 private:
  Waiter(S default_status)
      : internal::BaseWaiter<internal::ResultAccumulator<S, T>,
                             std::pair<S, std::vector<T>>,
                             S,
                             T>(
            internal::ResultAccumulator<S, T>(default_status)) {}
};

// StatusWaiter can be used to collate the results of many asynchronous calls
// into one callback. It is different from Waiter in that the callbacks only use
// S (e.g. storage::Status) as an argument.
template <class S>
class StatusWaiter
    : public internal::BaseWaiter<internal::StatusAccumulator<S>, S, S> {
 public:
  static ftl::RefPtr<StatusWaiter<S>> Create(S default_status) {
    return ftl::AdoptRef(new StatusWaiter<S>(default_status));
  }

 private:
  StatusWaiter(S default_status)
      : internal::BaseWaiter<internal::StatusAccumulator<S>, S, S>(
            internal::StatusAccumulator<S>(default_status)) {}
};

// CompletionWaiter can be used to be notified on completion of a computation.
class CompletionWaiter
    : public internal::BaseWaiter<internal::CompletionAccumulator, bool> {
 public:
  static ftl::RefPtr<CompletionWaiter> Create() {
    return ftl::AdoptRef(new CompletionWaiter());
  }

  void Finalize(std::function<void()> callback) {
    internal::BaseWaiter<internal::CompletionAccumulator,
                         bool>::Finalize([callback =
                                              std::move(callback)](
        bool result) { callback(); });
  }

 private:
  CompletionWaiter()
      : internal::BaseWaiter<internal::CompletionAccumulator, bool>(
            internal::CompletionAccumulator()) {}
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_WAITER_H_
