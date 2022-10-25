// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_CIRCULAR_QUEUE_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_CIRCULAR_QUEUE_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <memory>

// Only designed to work with POD types.
template <class T>
class CircularQueue {
 private:
  std::unique_ptr<T[]> contents_;
  uint32_t read_index_;
  uint32_t write_index_;
  uint32_t count_{0};
  uint32_t capacity_{0};

 public:
  CircularQueue() = default;
  virtual ~CircularQueue() = default;

  void Init(uint32_t capacity) {
    ZX_ASSERT_MSG(count_ == 0, "Init when not empty. There are still %u elements in the queue.",
                  count_);
    contents_.reset(capacity > 0 ? new T[capacity] : nullptr);
    capacity_ = capacity;
    read_index_ = 0;
    write_index_ = 0;
  }

  void Enqueue(T elem) {
    ZX_ASSERT_MSG(count_ < capacity_, "Enqueue when full.");
    contents_[write_index_] = elem;
    write_index_ = (write_index_ + 1) % capacity_;
    count_++;
  }

  const T& Front() {
    ZX_ASSERT_MSG(count_ > 0, "Front when empty.");
    return contents_[read_index_];
  }

  void Dequeue() {
    ZX_ASSERT_MSG(count_ > 0, "Dequeue when empty.");
    read_index_ = (read_index_ + 1) % capacity_;
    count_--;
  }

  uint32_t Count() { return count_; }
};

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_CIRCULAR_QUEUE_H_
