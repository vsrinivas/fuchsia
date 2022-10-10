// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_THREAD_SAFE_QUEUE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_THREAD_SAFE_QUEUE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <deque>
#include <mutex>
#include <optional>

namespace media_audio {

// A generic thread-safe queue. Safe for use with multiple producers and multiple consumers.
//
// The element type must be movable.
template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;
  ThreadSafeQueue(const ThreadSafeQueue&) = delete;
  ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
  ThreadSafeQueue(ThreadSafeQueue&&) = delete;
  ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

  // Pushes an item onto the end of the queue.
  void push(T item) {
    std::lock_guard<std::mutex> guard(mutex_);
    queue_.push_back(std::move(item));
  }

  // Pops an item from the front of the queue, or returns std::nullopt if the queue is empty.
  std::optional<T> pop() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    auto item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

 private:
  std::mutex mutex_;
  std::deque<T> queue_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_COMMON_THREAD_SAFE_QUEUE_H_
