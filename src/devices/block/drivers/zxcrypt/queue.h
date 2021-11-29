// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_QUEUE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_QUEUE_H_

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace zxcrypt {

template <typename T>
class Queue {
 public:
  void Terminate() {
    {
      std::scoped_lock lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
  }

  void Push(T value) {
    {
      std::scoped_lock lock(mutex_);
      queue_.push_back(value);
    }
    condition_.notify_one();
  }

  // Thread-safety analysis doesn't work with unique_lock.
  std::optional<T> Pop() FXL_NO_THREAD_SAFETY_ANALYSIS {
    std::unique_lock lock(mutex_);
    for (;;) {
      if (terminate_)
        return std::nullopt;
      if (!queue_.empty()) {
        auto result = std::move(queue_.front());
        queue_.pop_front();
        return result;
      }
      condition_.wait(lock);
    }
  }

 private:
  std::mutex mutex_;
  bool terminate_ FXL_GUARDED_BY(mutex_) = false;
  std::condition_variable condition_;
  std::deque<T> queue_ FXL_GUARDED_BY(mutex_);
};

}  // namespace zxcrypt

#endif  // SRC_DEVICES_BLOCK_DRIVERS_ZXCRYPT_QUEUE_H_
