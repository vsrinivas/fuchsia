// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_H_

#include <lib/ddk/io-buffer.h>
#include <lib/zx/bti.h>
#include <lib/zx/result.h>

#include <memory>

#include <hwreg/bitfields.h>

namespace nvme {

// Represents a single NVME queue in memory.
// The queue will always fit in one page.
class Queue {
 public:
  static zx::result<Queue> Create(zx::unowned_bti bti, size_t queue_id, size_t max_entries,
                                  size_t entry_size) {
    Queue ret(entry_size, queue_id);
    auto status = ret.Init(std::move(bti), max_entries);
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(std::move(ret));
  }

  // Get the physical address of this queue, suitable for passing to the controller.
  zx_paddr_t GetDeviceAddress() const { return io_.phys_list()[0]; }
  // Return the number of entries in the queue.
  size_t entry_count() const { return entry_count_; }

  // Get the next item in the queue, and move the queue pointer forward.
  void* Next() {
    void* value = static_cast<uint8_t*>(io_.virt()) + (next_index_ * entry_size_);
    next_index_++;
    if (next_index_ == entry_count_) {
      next_index_ = 0;
    }
    return value;
  }

  // Return the next item in the queue without affecting the queue.
  void* Peek() { return static_cast<uint8_t*>(io_.virt()) + (next_index_ * entry_size_); }

  // Return the index of the next item in the queue.
  size_t NextIndex() const { return next_index_; }

  // For unit tests only.
  void* head() const { return io_.virt(); }

 private:
  explicit Queue(size_t entry_size, size_t queue_id)
      : entry_size_(entry_size), queue_id_(queue_id) {}
  zx::result<> Init(zx::unowned_bti bti, size_t max_entries);

  ddk::IoBuffer io_;
  size_t entry_size_;
  size_t entry_count_;
  __UNUSED size_t queue_id_;

  size_t next_index_ = 0;
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_QUEUE_H_
