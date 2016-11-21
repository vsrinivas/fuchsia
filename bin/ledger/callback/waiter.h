// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_WAITER_H_
#define APPS_LEDGER_SRC_CALLBACK_WAITER_H_

#include <memory>
#include <vector>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace callback {
// Waiter can be used to collate the results of many asynchronous calls into one
// callback. A typical usage example would be:
// Waiter<Status, Object> waiter(Status::OK);
// storage->GetObject(object_id1, waiter.NewCallback());
// storage->GetObject(object_id2, waiter.NewCallback());
// storage->GetObject(object_id3, waiter.NewCallback());
// ...
// waiter->Finalize([](Status s, std::vector<std::unique_ptr<Object>> v) {
//   do something with the returned objects
// });
template <class S, class T>
class Waiter : public ftl::RefCountedThreadSafe<Waiter<S, T>> {
 public:
  static ftl::RefPtr<Waiter<S, T>> Create(S default_status) {
    return ftl::AdoptRef(new Waiter<S, T>(default_status));
  }

  std::function<void(S, std::unique_ptr<T>)> NewCallback() {
    FTL_DCHECK(!finalized_);
    if (result_status_ != default_status_) {
      return [](S status, std::unique_ptr<T> result) {};
    }
    results_.push_back(std::unique_ptr<T>());
    std::function<void(S, std::unique_ptr<T>)> callback = [
      waiter_ref = ftl::RefPtr<Waiter<S, T>>(this), index = results_.size() - 1
    ](S status, std::unique_ptr<T> result) {
      waiter_ref->ReturnResult(index, status, std::move(result));
    };
    return callback;
  }

  void Finalize(
      std::function<void(S, std::vector<std::unique_ptr<T>>)> callback) {
    FTL_DCHECK(!finalized_) << "Waiter already finalized, can't finalize more!";
    result_callback_ = callback;
    finalized_ = true;
    ExecuteCallbackIfFinished();
  }

 private:
  Waiter(S default_status)
      : default_status_(default_status), result_status_(default_status_) {}

  void ReturnResult(int index, S status, std::unique_ptr<T> result) {
    if (result_status_ != default_status_)
      return;
    if (status != default_status_) {
      result_status_ = status;
      results_.clear();
      returned_results_ = 0;
      ExecuteCallbackIfFinished();
      return;
    }
    if (result) {
      results_[index].swap(result);
    }
    returned_results_++;
    ExecuteCallbackIfFinished();
  }

  void ExecuteCallbackIfFinished() {
    FTL_DCHECK(!finished_) << "Waiter already finished.";
    if (finalized_ && results_.size() == returned_results_) {
      result_callback_(result_status_, std::move(results_));
      finished_ = true;
    }
  }

  bool finished_ = false;
  bool finalized_ = false;
  size_t current_index_ = 0;

  size_t returned_results_ = 0;
  std::vector<std::unique_ptr<T>> results_;
  S default_status_;
  S result_status_;

  std::function<void(S, std::vector<std::unique_ptr<T>>)> result_callback_;
};

// StatusWaiter can be used to collate the results of many asynchronous calls
// into one callback. It is different from Waiter in that the callbacks only use
// S (e.g. storage::Status) as an argument.
template <class S>
class StatusWaiter {
 public:
  StatusWaiter(S default_status)
      : waiter_(Waiter<S, bool>::Create(default_status)) {}

  std::function<void(S)> NewCallback() {
    return [callback = waiter_->NewCallback()](S s) {
      callback(s, std::make_unique<bool>(true));
    };
  }

  void Finalize(std::function<void(S)> callback) {
    waiter_->Finalize([callback](
        S s, std::vector<std::unique_ptr<bool>> values) { callback(s); });
  }

 private:
  ftl::RefPtr<Waiter<S, bool>> waiter_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StatusWaiter);
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_WAITER_H_
