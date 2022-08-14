// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_SYNC_HOST_H_
#define SRC_STORAGE_F2FS_SYNC_HOST_H_

namespace f2fs {

// Temporarily define sync_completion_t for compatibility tests on Linux.
class sync_completion_t {
 public:
  sync_completion_t() { waiter_.test_and_set(std::memory_order_relaxed); }
  sync_completion_t(const sync_completion_t &) = delete;
  sync_completion_t &operator=(const sync_completion_t &) = delete;
  sync_completion_t(const sync_completion_t &&) = delete;
  sync_completion_t &operator=(const sync_completion_t &&) = delete;
  // TODO : Need to wait until |timeout| expires.
  zx_status_t wait(zx_duration_t timeout) {
    while (waiter_.test(std::memory_order_acquire)) {
      waiter_.wait(true, std::memory_order_relaxed);
    }
    return ZX_OK;
  }
  void signal() {
    waiter_.clear(std::memory_order_release);
    waiter_.notify_all();
  }

 private:
  std::atomic_flag waiter_ = ATOMIC_FLAG_INIT;
};

zx_status_t sync_completion_wait(sync_completion_t *completion, zx_duration_t timeout);
void sync_completion_signal(sync_completion_t *completion);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_SYNC_HOST_H_
