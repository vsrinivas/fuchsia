// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_

#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/macros.h>

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

// The typical state transitions for the deque are as follows:
//                   <initial>
//                       v
//   Reset() -------> |kOpen| ------> Close()
//    ^                  v              v
// |kClosed| <--- Clear(),<empty> <- |kClosing|
//
// More comprehensively, all state transtions are listed in the following table. Starting with a
// given state and queue, calling specific methods will transition to the following states:
// +================++===========+===========+===========+===========+
// |        state_: || kOpen     | kClosing, | kClosing, |  kClosed  |
// |                ||           | empty     | nonempty  |           |
// +================++===========+===========+===========+===========+
// |        Close() || kClosing  | kClosing  | kClosing  | kClosed   |
// +----------------++-----------+-----------+-----------+-----------+
// |        Clear() || kClosed   | kClosed   | kClosed   | kClosed   |
// +----------------++-----------+-----------+-----------+-----------+
// | [Try]Receive() || kOpen     | kClosed   | kClosing  | kClosed   |
// +----------------++-----------+-----------+-----------+-----------+
// |        Reset() || kOpen     | kOpen     | kOpen     | kOpen     |
// +----------------++-----------+-----------+-----------+-----------+
//

enum AsyncDequeState {
  // Items may be sent and received.
  kOpen,

  // No new items may be sent, but items may be resent. Any queued items may be received.
  kClosing,

  // No items may be sent. Queued items are dropped.
  kClosed,
};

template <typename T>
class AsyncDeque {
 public:
  AsyncDeque() = default;
  ~AsyncDeque() = default;

  static AsyncDequePtr<T> MakePtr() { return std::make_shared<AsyncDeque<T>>(); }

  AsyncDequeState state() const { return state_; }

  // Takes ownership of |t|. If this object has not been closed, it will resume any pending futures
  // waiting to |Receive| an |T|. Barring any calls to |Resend|, the waiter(s) will |Receive| |T|s
  // in the order they were sent. Returns |ZX_ERR_BAD_STATE| if already closed.
  __WARN_UNUSED_RESULT zx_status_t Send(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (state_ != kOpen) {
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
  // have been resent will be |Receive|d before any other |T|s. Items can be resent when closing,
  // but not when fully closed; thus receivers should decide whether to |Resend| a previous item
  // before trying to |Receive| the next item.
  __WARN_UNUSED_RESULT zx_status_t Resend(T&& t) {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (state_ == kClosed) {
      return ZX_ERR_BAD_STATE;
    }
    auto completer = GetCompleter();
    if (completer) {
      completer.complete_ok(std::move(t));
    } else {
      queue_.emplace_front(std::move(t));
    }
    return ZX_OK;
  }

  // Returns a |T| if at least one has been sent using |Send| or |Resend| but not received, or an
  // error if none are available. If the queue is closing and no items remain, the queue  will
  // automatically be closed.
  ZxResult<T> TryReceive() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (state_ == kClosed) {
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    if (!completers_.empty()) {
      return fpromise::error(ZX_ERR_SHOULD_WAIT);
    }
    if (!queue_.empty()) {
      auto t = std::move(queue_.front());
      queue_.pop_front();
      return fpromise::ok(std::move(t));
    }
    if (state_ == kClosing) {
      Clear();
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    return fpromise::error(ZX_ERR_SHOULD_WAIT);
  }

  // Returns a promise to get a |T| once it has been (re-)sent. If this object is closing, this can
  // still return data that was "in-flight", i.e. (re-)sent but not yet |Receive|d. If this object
  // is closing and no items remain the queue  will automatically be closed.
  Promise<T> Receive() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    return fpromise::make_promise([this, generation = num_resets_,
                                   receive = Future<T>()](Context& context) mutable -> Result<T> {
             if (!receive) {
               if (generation != num_resets_) {
                 // |Reset| called before first execution.
                 return fpromise::error();
               }
               auto result = TryReceive();
               if (result.is_ok()) {
                 return fpromise::ok(result.take_value());
               }
               if (auto status = result.error(); status != ZX_ERR_SHOULD_WAIT) {
                 return fpromise::error();
               }
               Bridge<T> bridge;
               completers_.emplace_back(std::move(bridge.completer));
               receive = bridge.consumer.promise_or(fpromise::error());
             }
             if (!receive(context)) {
               return fpromise::pending();
             }
             return receive.take_result();
           })
        .wrap_with(scope_);
  }

  // Closes this object, preventing any further data from being sent. To use a theme-park analogy,
  // this is the "rope at the end of the line": no more items can join the queue, but those already
  // in the queue (and those added to the front via |Resend|) will still be processed.
  void Close() {
    FIT_DCHECK_IS_THREAD_VALID(thread_checker_);
    if (state_ == kOpen) {
      state_ = kClosing;
    }
  }

  // Close this object and drops all queued data.
  void Clear() {
    state_ = kClosed;
    completers_.clear();
    queue_.clear();
  }

  // Resets this object to its default state.
  void Reset() {
    Clear();
    num_resets_++;
    state_ = kOpen;
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

  AsyncDequeState state_ = kOpen;
  std::deque<Completer<T>> completers_;
  std::deque<T> queue_;
  uint64_t num_resets_ = 0;
  Scope scope_;

  FIT_DECLARE_THREAD_CHECKER(thread_checker_)
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncDeque);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
