// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_MPSC_QUEUE_MPSC_QUEUE_H_
#define SRC_MEDIA_LIB_MPSC_QUEUE_MPSC_QUEUE_H_

#include <lib/zx/event.h>
#include <zircon/assert.h>

#include <atomic>
#include <memory>
#include <optional>
#include <queue>
#include <stack>

// A lock free queue for multiple producers and a single consumer.
template <typename T>
class MpscQueue {
 public:
  MpscQueue() : cache_(nullptr), head_(nullptr) {}

  ~MpscQueue() { Clear(); }

  // Disallow copy, assign, and move.
  MpscQueue(MpscQueue&&) = delete;
  MpscQueue(const MpscQueue&) = delete;
  MpscQueue& operator=(MpscQueue&&) = delete;
  MpscQueue& operator=(const MpscQueue&) = delete;

  // Pushes a new element onto the queue.
  //
  // In any given thread, elements pushed first will be dequeued first. When
  // pushers on different threads contend it is not gauranteed that the thread
  // to call first will end up in the queue first.
  template <typename U>
  void Push(U&& element) {
    Cell* loaded_head;
    Cell* new_head = new Cell{.element = std::forward<T>(element)};
    do {
      loaded_head = head_.load();
      new_head->next = loaded_head;
    } while (!head_.compare_exchange_strong(loaded_head, new_head));
  }

  // Pops an element from the queue.
  //
  // This should only be called from the consumer thread.
  std::optional<T> Pop() {
    if (!cache_) {
      cache_ = TakeHead();
    }

    if (!cache_) {
      return std::nullopt;
    }

    T elem = std::move(cache_->element);
    Cell* to_delete = cache_;
    cache_ = cache_->next;
    delete to_delete;
    return elem;
  }

  // Drops all elements from the queue.
  //
  // This should only be called from the consumer thread.
  void Clear() {
    while (Pop()) {
    }
  }

 private:
  struct Cell {
    T element;
    Cell* next;
  };

  Cell* TakeHead() {
    Cell* node;
    do {
      node = head_.load();
    } while (!head_.compare_exchange_strong(node, nullptr));

    if (!node) {
      return nullptr;
    }

    Cell* prev = nullptr;
    while (node) {
      Cell* tmp = node;
      node = node->next;
      tmp->next = prev;
      prev = tmp;
    }

    return prev;
  }

  Cell* cache_;
  std::atomic<Cell*> head_;
};

// A multiproducer single consumer queue which blocks for the consumer.
template <typename T>
class BlockingMpscQueue {
 public:
  // Deconstructs the queue and returns all its elements.
  //
  // This should only be called on the consumer thread.
  static std::queue<T> Extract(BlockingMpscQueue&& queue) {
    queue.StopAllWaits();
    std::queue<T> elements;
    std::optional<T> element;
    while ((element = queue.queue_.Pop())) {
      elements.push(std::move(*element));
    }
    return elements;
  }

  BlockingMpscQueue() : should_wait_(true) {
    zx_status_t status = zx::event::create(0, &should_wait_event_);
    ZX_ASSERT(status == ZX_OK);
  }

  template <typename U>
  void Push(U&& element) {
    queue_.Push(std::forward<T>(element));
    should_wait_event_.signal(ZX_EVENT_SIGNAL_MASK, ZX_EVENT_SIGNALED);
  }

  // Stops all waiting threads. We call this when a stream is stopped to abort
  // the input processing loop.
  void StopAllWaits() {
    should_wait_.store(false);
    should_wait_event_.signal(ZX_EVENT_SIGNAL_MASK, ZX_EVENT_SIGNALED);
  }

  // Resets the queue to its default state.
  void Reset(bool keep_data = false) {
    should_wait_.store(true);
    if (!keep_data) {
      queue_.Clear();
    }
  }

  // Get an element or block until one is available if the queue is empty.
  // If a thread calls StopAllWaits(), std::nullopt is returned.
  //
  // This should only be called on the consumer thread.
  std::optional<T> WaitForElement() {
    std::optional<T> element;

    while (should_wait_ && !(element = queue_.Pop())) {
      should_wait_event_.wait_one(ZX_EVENT_SIGNALED, zx::time(ZX_TIME_INFINITE), nullptr);
    }

    should_wait_event_.signal(ZX_EVENT_SIGNALED, 0);

    return element;
  }

  // Returns true if queue has been pushed to but WaitForElement has not yet been called
  bool Signaled() {
    zx_signals_t signals = 0;
    should_wait_event_.wait_one(0, zx::time{}, &signals);
    return signals & ZX_EVENT_SIGNALED;
  }

 private:
  zx::event should_wait_event_;
  std::atomic<bool> should_wait_;
  MpscQueue<T> queue_;
};

#endif  // SRC_MEDIA_LIB_MPSC_QUEUE_MPSC_QUEUE_H_
