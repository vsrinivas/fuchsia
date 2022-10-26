// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// `AsyncSender` and `AsyncReceiver` are paired objects that act as asynchronous pipelines to move
// objects from one future to another. A receiver may have multiple senders (via
// `AsyncSender::Clone`); each sender has a single receiver.
//
// Both the senders and receiver are backed by a shared `AsyncDeque`. This class cannot be directly
// constructed. Instead, create and pass a sender to the receiver's constructor to initialize it:
//
//   AsyncSender<Foo> sender;
//   AsyncReceiver<Foo> receiver(&sender);
//
// The senders and receivers are movable and thread-safe.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_

#include <lib/syslog/cpp/macros.h>

#include <deque>
#include <mutex>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// Forward declarations.
enum AsyncDequeState : uint16_t;

template <typename T>
class AsyncSender;

template <typename T>
class AsyncReceiver;

template <typename T>
class AsyncDeque;

// Sending half of an async deque.
//
// This class is thread-safe.
//
template <typename T>
class AsyncSender final {
 public:
  AsyncSender() = default;

  // Since `AsyncDeque<T>` cannot be directly constructed, this constructor cannot be used by
  // callers. Instead, create senders with receivers using the constructor for `AsyncReceiver<T>`.
  explicit AsyncSender(std::shared_ptr<AsyncDeque<T>> deque) : deque_(deque) {
    if (deque_) {
      deque_->AddSender();
    }
  }

  AsyncSender(AsyncSender<T>&& other) noexcept { *this = std::move(other); }
  AsyncSender<T>& operator=(AsyncSender<T>&& other) noexcept {
    deque_ = std::move(other.deque_);
    other.deque_.reset();
    return *this;
  }

  ~AsyncSender() {
    if (deque_) {
      deque_->RemoveSender();
    }
  }

  // Takes ownership of an `item` and transfers it to a caller of `AsyncReceiver<T>::Receive` on the
  // receiver with the same underlying deque. If there are outstanding callers, the item is
  // delivered to the earliest one, otherwise it will be delivered to the next caller. Returns
  // `ZX_ERR_PEER_CLOSED` if the underlying deque is already closed.
  __WARN_UNUSED_RESULT zx_status_t Send(T item) {
    return deque_ ? deque_->Send(std::move(item)) : ZX_ERR_BAD_STATE;
  }

  // Returns a new sender that sends items to the same receiver as this object.
  AsyncSender<T> Clone() { return AsyncSender(deque_); }

 private:
  std::shared_ptr<AsyncDeque<T>> deque_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AsyncSender);
};

// Alias to make it easier to move receivers.
template <typename T>
using AsyncReceiverPtr = std::unique_ptr<AsyncReceiver<T>>;

// Receiving half of an async deque.
//
// This class is thread-safe.
//
template <typename T>
class AsyncReceiver final {
 public:
  // Creates a receiver and returns its associated sender via `out`.
  explicit AsyncReceiver(AsyncSender<T>* out) : deque_(new AsyncDeque<T>()) {
    *out = AsyncSender<T>(deque_);
  }

  static AsyncReceiverPtr<T> MakePtr(AsyncSender<T>* out) {
    return std::make_unique<AsyncReceiver<T>>(out);
  }

  ~AsyncReceiver() { deque_->Clear(); }

  // Returns a promise to get an item once it has been sent. If this underlying object is closed, it
  // can still return data that was "in-flight", i.e. sent but not yet `Receive`d. If the object is
  // closed and no more data remains, all outstanding promises returned by `Receive`s will return an
  // error.
  Promise<T> Receive() { return deque_->Receive(); }

  // Closes the underlying object, preventing any further items from being sent. To use a theme-park
  // analogy, this is the "rope at the end of the line": no more items can join the queue, but those
  // already in the queue will still be processed.
  void Close() { deque_->Close(); }

  // Close this object and drops all queued items and pending calls to `Receive`.
  void Clear() { deque_->Clear(); }

  // Clears this object and resets it to a default, open state.
  void Reset() { deque_->Reset(); }

 private:
  std::shared_ptr<AsyncDeque<T>> deque_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncReceiver);
};

// Base class shared by `AsyncSender` and `AsyncReceiver`.
template <typename T>
class AsyncDeque {
 public:
  ~AsyncDeque() = default;

  // See `AsyncSender::Send`.
  zx_status_t Send(T&& t) FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    if (closed_) {
      return ZX_ERR_PEER_CLOSED;
    }
    while (!completers_.empty() && completers_.front().was_canceled()) {
      completers_.pop_front();
    }
    if (completers_.empty()) {
      queue_.emplace_back(std::move(t));
    } else {
      auto completer = std::move(completers_.front());
      completers_.pop_front();
      completer.complete_ok(std::move(t));
    }
    return ZX_OK;
  }

  // See `AsyncReceiver::Receive`.
  Promise<T> Receive() FXL_LOCKS_EXCLUDED(mutex_) {
    uint64_t generation = 0;
    {
      std::lock_guard lock(mutex_);
      generation = num_resets_;
    }
    return fpromise::make_promise(
               [this, generation, receive = Future<T>()](Context& context) mutable -> Result<T> {
                 std::lock_guard lock(mutex_);
                 if (!receive) {
                   if (generation != num_resets_) {
                     // `Reset` called before first execution.
                     return fpromise::error();
                   }
                   if (closed_ && queue_.empty()) {
                     // No data forthcoming.
                     completers_.clear();
                     return fpromise::error();
                   }
                   if (completers_.empty() && !queue_.empty()) {
                     auto t = std::move(queue_.front());
                     queue_.pop_front();
                     return fpromise::ok(std::move(t));
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

  // See the constructor for `AsyncSender`.
  void AddSender() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    ++num_senders_;
  }

  // See the destructor for `AsyncSender`.
  void RemoveSender() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    FX_CHECK(num_senders_ != 0);
    --num_senders_;
    if (num_senders_ == 0) {
      closed_ = true;

      // Note that exactly one of `completers_` and `queue_` is non-empty at any point. If
      // `completers_` is non-empty here, then `queue_` must be empty, and dropping the completers
      // causes the consumers to return errors, as per `Receive`.
      completers_.clear();
    }
  }

  // See `AsyncReceiver::Close`.
  void Close() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    closed_ = true;
  }

  // See `AsyncReceiver::Clear`.
  void Clear() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    completers_.clear();
    queue_.clear();
    closed_ = true;
  }

  // See `AsyncReceiver::Reset`.
  void Reset() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard lock(mutex_);
    completers_.clear();
    queue_.clear();
    num_resets_++;
    closed_ = false;
  }

 private:
  // Only the receiver is allowed to create the underlying `AsyncDeque<T>`.
  friend class AsyncReceiver<T>;
  AsyncDeque() = default;

  std::mutex mutex_;

  // Represent outstanding calls to `Receive` that are waiting for items to be sent using `Send`.
  std::deque<Completer<T>> completers_ FXL_GUARDED_BY(mutex_);

  // Represent items provided to `Send` that are waiting to be `Receive`d.
  std::deque<T> queue_ FXL_GUARDED_BY(mutex_);

  // Number of senders. See also `AsyncSender<T>::Clone`.
  uint64_t num_senders_ FXL_GUARDED_BY(mutex_) = 0;

  // Number of resets. Used to detect calls to `Receive` that span a call to `Reset`.
  uint64_t num_resets_ FXL_GUARDED_BY(mutex_) = 0;

  // Indicates if `Send`ing additional items is disallowed.
  bool closed_ FXL_GUARDED_BY(mutex_) = false;

  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncDeque);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_DEQUE_H_
