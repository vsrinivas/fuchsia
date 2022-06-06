// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_QUEUE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_QUEUE_H_

#include <lib/stdcompat/span.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <array>
#include <deque>
#include <functional>

#include "frame.h"
#include "frame_container.h"

namespace wlan::drivers::components {

// A class for maintaining a queue of frames ordered by frame priority.
//
// The queue also supports popping only certain allowed priority levels as identified by a bitmask.
// The queue does not implement any locking, the user will have to ensure that calls to push and pop
// are mutually exclusive.
class PriorityQueue {
 public:
  explicit PriorityQueue(size_t max_queue_depth) : max_queue_depth_(max_queue_depth) {}

  // Push a frame onto the queue. The frame's Priority() will be used to determine the priority in
  // the queue. Returns true on success, if a lower priority frame was evicted as a result of this
  // push the evicted frame will be placed in |dropped|. This allows the caller to return the
  // evicted frame to storage or netdevice as needed. If this parameter is used it should be an
  // uninitialized std::unique_ptr and afterwards the caller should check to see if it's still empty
  // or not to determine if eviction happened. Returns false if the frame is not placed on the
  // queue. When this happens |frame| is moved into |dropped| (if provided) since a moved from
  // object should not be used. Similarly to the eviction case this allows the caller to safely
  // return the frame to storage or complete it.
  bool push(Frame&& frame, std::unique_ptr<Frame>* dropped = nullptr);

  // Pop |count| number of frames from the queue. |allowed_priorities| specify a bit field
  // indicating which priorities are allowed to be popped from. Each bit index corresponds to a
  // priority value. The number of frames popped may be less than |count| if there are not enough
  // frames available. Note that frames are appended at the end of |frames|, the container will NOT
  // be cleared first. This allows the caller to call pop multiple times with the same container
  // with various priorities and create a customized ordering, but it also requires that the caller
  // ensures the container is empty before each call if they expect only the popped frames to be
  // present.
  void pop(size_t count, uint8_t allowed_priorities, FrameContainer* frames);

  // Pop any number of frames that match |predicate|. Frames will be evaluated, popped and appended
  // in priority order. |predicate| will be called for each frame and if |predicate| returns true
  // then the frame will be poppped. This is a slow operation and should not be used in a regular
  // data path, only for rare cases where specific frames need to be popped or removed. Note that
  // frames are appended at the end of |frames|, the container will NOT be cleared first. This
  // allows the caller to call pop_if multiple times with the same container with various conditions
  // and create a customized ordering, but it also requires that the caller ensures the container is
  // empty before each call if they expect only the popped frames to be present.
  void pop_if(std::function<bool(const Frame&)>&& predicate, FrameContainer* frames);

  // Return the total number of frames in the queue that match the allowed priorities given. This is
  // represented as a bit field where each bit index corresponds to a priority value.
  size_t size(uint8_t allowed_priorities) const {
    size_t size = 0;
    for (uint8_t priority = 0; priority <= max_priority_; ++priority) {
      if (allowed_priorities & (1 << priority)) {
        size += queues_[priority].size();
      }
    }

    return size;
  }

  // Return the total number of frames for all priorities.
  size_t size() const { return current_queue_depth_; }

  bool empty() const { return current_queue_depth_ == 0; }

  size_t capacity() const { return max_queue_depth_; }

 private:
  // Since this priority queue is intended for use with 802.1q priorities we can limit the number
  // of possible priorities to eight.
  static constexpr size_t kNumPriorities = 8;
  std::array<std::deque<Frame>, kNumPriorities> queues_;
  size_t current_queue_depth_ = 0;
  size_t max_queue_depth_;
  uint8_t max_priority_ = 0;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_QUEUE_H_
