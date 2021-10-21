// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_QUEUE_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_QUEUE_H_

#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>

#include <deque>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace fmlib {

// Errors returned by |StreamQueue::pull|.
enum class StreamQueueError {
  // |cancel_pull| was called.
  kCanceled,

  // |drain| was called, and all elements have been pulled from the queue.
  kDrained,
};

// Thread-safe, single-producer, single-consumer queue intended for media streams. |T| is the packet
// type, which must be movable. |U| is the clear request type, which must also be moveable.
//
// A queue element can be a packet, a clear request, or an 'ended' signal. All elements pass
// through the queue on a first-in, first-out basis with the exception of clear requests.
//
// A packet represents a fragment of the stream corresponding to some interval time. The packet
// type is a template parameter to allow this container to be used with a variety of packet
// implementations.
//
// A clear request is used to clear a pipeline. When a clear request is added to the queue, all
// elements in the queue other than clear requests are removed from the queue and destroyed.
// Clear requests are intended to be forwarded downstream to clear an entire pipeline. The clear
// request type is a template parameter to allow this container to be used with a variety of
// clear request implementations.
//
// An 'ended' signal marks the end of a stream.
//
// TODO(dalesat): lockless?
// TODO(dalesat): multi-consumer?
//
template <typename T, typename U>
class StreamQueue {
 public:
  using PacketType = T;
  using ClearRequestType = U;

  // Type for elements of a queue. An |Element| may contain a packet, a clear request, or an
  // end-of-stream indication.
  class Element {
   public:
    enum Tag { kPacket, kClearRequest, kEnded };

    // Constructs an element containing a packet.
    explicit Element(T packet) : state_(std::in_place_index<Tag::kPacket>, std::move(packet)) {}

    // Constructs an element containing a clear request.
    explicit Element(U clear_request)
        : state_(std::in_place_index<Tag::kClearRequest>, std::move(clear_request)) {}

    // Returns an element containing an end-of-stream indication.
    static Element Ended() { return Element(); }

    // Returns the tag classifying the content of this |Element|.
    constexpr Tag tag() const { return static_cast<Tag>(state_.index()); }

    // Determines whether this element contains a packet.
    constexpr bool is_packet() const { return tag() == Tag::kPacket; }

    // Determines whether this element contains a clear request.
    constexpr bool is_clear_request() const { return tag() == Tag::kClearRequest; }

    // Determines whether this element contains an end-of-stream indication.
    constexpr bool is_ended() const { return tag() == Tag::kEnded; }

    // Returns a const reference to the contained packet. May only be called if this |Element|
    // contains a packet.
    constexpr T& packet() { return std::get<Tag::kPacket>(state_); }

    // Takes (moves) the contained packet. May only be called if this |Element| contains a packet.
    T take_packet() { return std::move(std::get<Tag::kPacket>(state_)); }

    // Returns a const reference to the contained clear request. May only be called if this
    // |Element| contains a clear request.
    constexpr U& clear_request() { return std::get<Tag::kClearRequest>(state_); }

    // Takes (moves) the contained clear request. May only be called if this |Element| contains a
    // clear request.
    U take_clear_request() { return std::move(std::get<Tag::kClearRequest>(state_)); }

   private:
    // Constructs an |Element| containing an end-of-stream indication.
    Element() : state_(std::in_place_index<Tag::kEnded>) {}

    std::variant<T, U, std::monostate> state_;
  };

  // Constructs an empty queue.
  StreamQueue() = default;

  // Destructs a queue.
  ~StreamQueue() { cancel_pull(); }

  // Disallow copy, assign, and move.
  StreamQueue(StreamQueue&&) = delete;
  StreamQueue(const StreamQueue&) = delete;
  StreamQueue& operator=(StreamQueue&&) = delete;
  StreamQueue& operator=(const StreamQueue&) = delete;

  // Pushes a packet to the tail of the queue.
  template <typename V>
  void push(V&& packet) {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!draining_);

    if (pull_completer_) {
      FX_CHECK(deque_.empty());
      pull_completer_.complete_ok(Element(std::forward<T>(packet)));
      return;
    }

    deque_.push_back(Element(std::forward<T>(packet)));
  }

  // Pushes a packet to the tail of the queue. Viable for copyable packet types only.
  // TODO(dalesat): This version could be removed if T is always move-only (as opposed to moveable).
  // The unit tests use uint32_t for T, so changing that is a prerequisite.
  template <typename V>
  void push(const V& packet) {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!draining_);

    if (pull_completer_) {
      FX_CHECK(deque_.empty());
      pull_completer_.complete_ok(Element(packet));
      return;
    }

    deque_.push_back(Element(packet));
  }

  using PullResult = fpromise::result<Element, StreamQueueError>;

  // Returns a promise that completes with the element at the front of the queue, removing it on
  // completion. After this method is called, it may not be called again until after the promise
  // completes.
  [[nodiscard]] fpromise::promise<Element, StreamQueueError> pull() {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!pull_completer_) << "pull() was called before the previous call completed.";

    if (!deque_.empty()) {
      auto result = fpromise::make_result_promise<Element, StreamQueueError>(
                        fpromise::ok(std::move(deque_.front())))
                        .box();
      deque_.pop_front();
      return result;
    }

    if (draining_) {
      return fpromise::make_result_promise<Element, StreamQueueError>(
                 fpromise::error(StreamQueueError::kDrained))
          .box();
    }

    fpromise::bridge<Element, StreamQueueError> bridge;
    pull_completer_ = std::move(bridge.completer);
    return bridge.consumer.promise();
  }

  // Sets a closure that is called whenever a |clear| method is called. Pass a null |closure| to
  // deregister a previously-registered closure.
  //
  // This method is typically used when the thread that calls |pull| may be blocked when |clear| is
  // called, and another thread must take action to unblock that thread so that the clear operation
  // may propagate. Threads that process packets in software will often need to block pending memory
  // allocation from the output side, because the third-party software on which they are based calls
  // out synchronously for allocation of output buffers.
  void set_cleared_closure(fit::closure closure) {
    std::lock_guard<std::mutex> locker(mutex_);
    cleared_closure_ = std::move(closure);
  }

  // Cancels the previously-created |pull| promise and returns true. Returns false if there is no
  // |pull| promise pending.
  bool cancel_pull() {
    std::lock_guard<std::mutex> locker(mutex_);
    if (!pull_completer_) {
      return false;
    }

    pull_completer_.complete_error(StreamQueueError::kCanceled);
    return true;
  }

  // Clears the queue of all packets and end-of-stream elements and enqueues a |kCleared|
  // element.
  template <typename V>
  void clear(V&& clear_request) {
    fit::closure cleared_closure;

    {
      std::lock_guard<std::mutex> locker(mutex_);
      FX_CHECK(!draining_);

      if (cleared_closure_) {
        cleared_closure = cleared_closure_.share();
      }

      if (pull_completer_) {
        FX_CHECK(deque_.empty());
        pull_completer_.complete_ok(Element(std::forward<U>(clear_request)));
        return;
      }

      clear_internal();
      deque_.push_back(Element(std::forward<U>(clear_request)));
    }

    if (cleared_closure) {
      cleared_closure();
    }
  }

  // Clears the queue of all packets and end-of-stream elements and enqueues a |kCleared|
  // element. Viable for copyable clear request types only.
  // TODO(dalesat): As with pull() above, this version can be removed if U is move-only.
  template <typename V>
  void clear(V& clear_request) {
    fit::closure cleared_closure;

    {
      std::lock_guard<std::mutex> locker(mutex_);
      FX_CHECK(!draining_);

      if (cleared_closure_) {
        cleared_closure = cleared_closure_.share();
      }

      if (pull_completer_) {
        FX_CHECK(deque_.empty());
        pull_completer_.complete_ok(clear_request);
        return;
      }

      clear_internal();
      deque_.push_back(clear_request);
    }

    if (cleared_closure) {
      cleared_closure();
    }
  }

  // Enqueues a |kEnded| element.
  void end() {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!draining_);

    if (pull_completer_) {
      FX_CHECK(deque_.empty());
      pull_completer_.complete_ok(Element::Ended());
      return;
    }

    deque_.push_back(Element::Ended());
  }

  // Starts draining the queue. After this method is called, |push|, |clear|, |end| and |drain|
  // may not be called. After this method is called and the queue is empty, the promise returned
  // by |pull| will return |StreamQueueError::kDrained|.
  void drain() {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_CHECK(!draining_);

    draining_ = true;

    if (pull_completer_) {
      FX_CHECK(deque_.empty());
      pull_completer_.complete_error(StreamQueueError::kDrained);
      return;
    }
  }

  // Returns true if and only if the queue is empty.
  bool empty() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return deque_.empty();
  }

  // Returns the number of elements in the queue.
  size_t size() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return deque_.size();
  }

  // Returns true if and only if |drain| has been called and the queue is empty.
  bool is_drained() const {
    std::lock_guard<std::mutex> locker(mutex_);
    return draining_ && deque_.empty();
  }

 private:
  // Removes all packet and end-of-stream elements from the queue.
  void clear_internal() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    while (!deque_.empty()) {
      if (deque_.back().is_clear_request()) {
        break;
      }

      deque_.pop_back();
    }
  }

  mutable std::mutex mutex_;
  std::deque<Element> deque_ FXL_GUARDED_BY(mutex_);
  fpromise::completer<Element, StreamQueueError> pull_completer_ FXL_GUARDED_BY(mutex_);
  fit::closure cleared_closure_ FXL_GUARDED_BY(mutex_);
  bool draining_ FXL_GUARDED_BY(mutex_) = false;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_QUEUE_H_
