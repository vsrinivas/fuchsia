// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_

#include <lib/fit/thread_checker.h>
#include <zircon/compiler.h>

#include <deque>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// The |AsyncDeque| class facilitates creating asynchronous pipelines which move objects from one
// future to another.
//
// While this class can be used by multiple futures, it is *not* thread-safe, i.e. all futures
// must share a common executor.

// Alias to make it easier to share the queue between producers and consumers.
template <typename T>
class AsyncDeque;
template <typename T>
using AsyncDequePtr = std::shared_ptr<AsyncDeque<T>>;

template <typename T>
class AsyncDeque {
 public:
  AsyncDeque() = default;
  ~AsyncDeque() = default;

  bool is_closed() const { return closed_; }
  bool is_empty() const { return queue_.empty(); }

  static AsyncDequePtr<T> MakePtr() { return std::make_shared<AsyncDeque<T>>(); }

  // Takes ownership of |t|. If this object has not been closed, it will resume any pending futures
  // waiting to |Receive| an |T|. Barring any calls to |Resend|, the waiter(s) will |Receive| |T|s
  // in the order they were sent. Returns |ZX_ERR_BAD_STATE| if already closed.
  __WARN_UNUSED_RESULT zx_status_t Send(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (closed_) {
      return ZX_ERR_BAD_STATE;
    }
    auto completer = GetCompleter();
    if (completer) {
      completer.complete_ok(std::move(t));
    } else {
      queue_.emplace_back(std::move(t));
    }
    return ZX_OK;
  }

  // Takes ownership of |t| and resumes any pending futures waiting to |Receive| an |T|. |T|s that
  // have been resent will be |Receive|d before any other |T|s. It is always possible to |Resend|,
  // even when the deque has been |Close|d to new items.
  void Resend(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    auto completer = GetCompleter();
    if (completer) {
      completer.complete_ok(std::move(t));
    } else {
      queue_.emplace_front(std::move(t));
    }
  }

  // Returns a promise to get a |T| once it has been (re-)sent. If this object is closed, this can
  // still return data that was "in-flight", i.e. (re-)sent but not yet |Receive|d.
  //
  // NOTE: Since the returned promise depends on the state of this object *at the time of the call*,
  // it is inadvisable to initialize futures within lambda-captures with a call to |Receive|. In
  // other words, this may produce unexpected results:
  //
  //   return fpromise::make_promise([get_t = Future<T>(t_queue.Receive())]
  //       (Context& context) mutable -> Result<T> {
  //     if(!get_t(context)) {
  //       return fpromise::pending();
  //     }
  //     return get_t.take_result();
  //   }
  //
  // This can cause problems for queues that are closed, cleared, and/or reset. Instead, prefer to
  // initialize the future within the body of the handler itself:
  //
  //   return fpromise::make_promise([get_t = Future<T>()]
  //       (Context& context) mutable -> Result<T> {
  //     if (!get_t) {
  //       get_t  = t_queue.Receive();
  //     }
  //     if(!get_t(context)) {
  //       return fpromise::pending();
  //     }
  //     return get_t.take_result();
  //   }
  //
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

  // Closes this object, preventing any further data from being sent. To use a theme-park analogy,
  // this is the "rope at the end of the line": no more items can join the queue, but those already
  // in the queue (and those added to the front via |Resend|) will still be processed.
  void Close() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    closed_ = true;
    completers_.clear();
  }

  // Close this object and drops all queued data.
  void Clear() {
    Close();
    queue_.clear();
  }

  // Resets this object to its default state.
  void Reset() {
    Clear();
    closed_ = false;
  }

 private:
  // Returns the next completer that hasn't been canceled, or an empty completer if none available.
  Completer<T> GetCompleter() {
    while (!completers_.empty() && completers_.front().was_canceled()) {
      completers_.pop_front();
    }
    if (completers_.empty()) {
      return Completer<T>();
    }
    auto completer = std::move(completers_.front());
    completers_.pop_front();
    return completer;
  }

  std::deque<Completer<T>> completers_;
  std::deque<T> queue_;
  bool closed_ = false;
  Scope scope_;

  FIT_DECLARE_THREAD_CHECKER(thread_checker_)
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncDeque);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
