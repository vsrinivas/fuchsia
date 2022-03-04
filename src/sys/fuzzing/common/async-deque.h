// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_

#include <lib/fit/thread_checker.h>

#include <deque>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// The |AsyncDeque| class facilitates creating asynchronous pipelines which move objects from one
// future to another.
//
// While this class can be used by multiple futures, it is *not* thread-safe, i.e. all futures
// must share a common executor.
//
template <typename T>
class AsyncDeque {
 public:
  AsyncDeque() = default;
  ~AsyncDeque() = default;

  // Takes ownership of |t|. If this object has not been closed, it will resume any pending futures
  // waiting to |Receive| an |T|. Barring any calls to |Resend|, the waiter(s) will |Receive| |T|s
  // in the order they were sent.
  void Send(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (closed_) {
      return;
    }
    if (completers_.empty()) {
      queue_.emplace_back(std::move(t));
    } else {
      completers_.front().complete_ok(std::move(t));
    }
  }

  // Takes ownership of |t|. If this object has not been closed, it will resume any pending futures
  // waiting to |Receive| an |T|. |T|s that have been resent will be |Receive|d before any other
  // |T|s.
  void Resend(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (closed_) {
      return;
    }
    if (completers_.empty()) {
      queue_.emplace_front(std::move(t));
    } else {
      completers_.front().complete_ok(std::move(t));
    }
  }

  // Returns a promise to get a |T| once it has been (re-)sent. If this object is closed, this can
  // still return data that was "in-flight", i.e. (re-)sent but not yet |Receive|d.
  Promise<T> Receive() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (completers_.empty() && !queue_.empty()) {
      auto t = std::move(queue_.front());
      queue_.pop_front();
      return fpromise::make_result_promise<T>(fpromise::ok(std::move(t)));
    }
    if (closed_) {
      return fpromise::make_result_promise<T>(fpromise::error());
    }
    fpromise::bridge<T> bridge;
    completers_.emplace_back(std::move(bridge.completer));
    return bridge.consumer.promise_or(fpromise::error())
        .and_then([](T& t) -> Result<T> { return fpromise::ok(std::move(t)); })
        .or_else([] { return fpromise::error(); })
        .wrap_with(scope_);
  }

  // Closes this object, preventing any further data from being (re-)sent.
  void Close() {
    closed_ = true;
    completers_.clear();
  }

 private:
  std::deque<fpromise::completer<T>> completers_;
  std::deque<T> queue_;
  bool closed_ = false;
  Scope scope_;

  FIT_DECLARE_THREAD_CHECKER(thread_checker_)
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncDeque);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
