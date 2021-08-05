// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_UTILS_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_UTILS_H_

#include <string.h>
#include <sys/types.h>

#include <mutex>
#include <queue>

#include "src/lib/fxl/synchronization/thread_annotations.h"

using memset_fn_t = void* (*)(void*, int, size_t);
static volatile memset_fn_t memset_fn = memset;

// secure_memset is similar to memset except that it won't be optimized away.
// Refer to https://en.cppreference.com/w/c/string/byte/memset.
inline void* secure_memset(void* ptr, int c, size_t len) { return memset_fn(ptr, c, len); }

// A generic Queue implementation which limits number of items
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t max_size) : max_size_(std::max(static_cast<size_t>(1), max_size)) {}

  // Add entry to BoundedQueue. If queue is already full
  // an item from front of the queue will be removed.
  template <typename... Args>
  T& AddEntry(Args&&... args) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() == max_size_) {
      queue_.pop();
    }
    queue_.emplace(std::forward<Args>(args)...);
    return queue_.back();
  }

 private:
  size_t max_size_;
  std::mutex mutex_;
  std::queue<T> queue_ FXL_GUARDED_BY(mutex_);
};

// Helper function to determine largest value as a constexpr (base case).
template <typename T>
constexpr const T& MaxConstant(const T& a, const T& b) {
  return (a > b) ? a : b;
}

// Helper function to determine largest value as a constexpr (variadic).
template <typename T, typename... Args>
constexpr const T& MaxConstant(const T& a, const T& b, const Args&... args) {
  return MaxConstant(MaxConstant(a, b), args...);
}
#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_UTILS_H_
