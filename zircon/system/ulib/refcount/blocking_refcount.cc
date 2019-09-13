// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <limits>

#include <refcount/blocking_refcount.h>

namespace refcount {

BlockingRefCount::BlockingRefCount() : count_(0) {}

BlockingRefCount::BlockingRefCount(int32_t initial_count) : count_(initial_count) {
  ZX_DEBUG_ASSERT(initial_count >= 0);
}

void BlockingRefCount::Inc() {
  sync_mutex_lock(&lock_);
  ZX_DEBUG_ASSERT(count_ >= 0);
  ZX_DEBUG_ASSERT(count_ < std::numeric_limits<decltype(count_)>::max());
  count_++;
  sync_mutex_unlock(&lock_);
}

void BlockingRefCount::Dec() {
  sync_mutex_lock(&lock_);
  ZX_DEBUG_ASSERT(count_ > 0);
  count_--;
  if (count_ == 0) {
    sync_condition_broadcast(&condition_);
  }
  sync_mutex_unlock(&lock_);
}

void BlockingRefCount::WaitForZero() const {
  sync_mutex_lock(&lock_);
  ZX_DEBUG_ASSERT(count_ >= 0);
  while (count_ > 0) {
    sync_condition_wait(&condition_, &lock_);
  }
  sync_mutex_unlock(&lock_);
}

}  // namespace refcount
