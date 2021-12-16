// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/lib/components/cpp/include/wlan/drivers/components/priority_queue.h"

namespace wlan::drivers::components {

bool PriorityQueue::push(Frame&& frame) {
  const uint8_t priority = frame.Priority();
  if (unlikely(priority >= kNumPriorities)) {
    return false;
  }
  if (unlikely(current_queue_depth_ >= max_queue_depth_)) {
    // This frame potentially has a higher priority than the lowest priority frame in our queue.
    // Check all priorities lower than this priority to see if there is anything to evict. This is
    // an unusual case in most situations as a driver will generally match its TX queue depth with
    // the size of this queue. The TX queue depth should be respected by upper layers in the
    // networking stack so the capacity of this queue should rarely be exceeded except possibly for
    // frames that are sent outside of the netstack data plane (such as EAPOL frames). This is a
    // very unlikely situation however and so we rarely pay the price for this eviction.
    bool evicted = false;
    for (uint8_t i = 0; i < priority; ++i) {
      if (!queues_[i].empty()) {
        queues_[i].pop_front();
        --current_queue_depth_;
        evicted = true;
        break;
      }
    }
    if (!evicted) {
      return false;
    }
    // If we evicted a frame, proceed as usual, there's now room
  }
  queues_[priority].emplace_back(std::move(frame));
  max_priority_ = std::max(priority, max_priority_);
  ++current_queue_depth_;
  return true;
}

cpp20::span<Frame> PriorityQueue::pop(size_t count, uint8_t allowed_priorities) {
  outgoing_.clear();

  while (count > 0) {
    uint8_t priority = max_priority_;
    while ((allowed_priorities & (1 << priority)) == 0 || queues_[priority].empty()) {
      if (priority == 0) {
        return cpp20::span<Frame>(outgoing_);
      }
      --priority;
    }

    auto& queue = queues_[priority];

    const size_t to_copy = std::min<size_t>(count, queue.size());
    for (size_t i = 0; i < to_copy; ++i) {
      outgoing_.emplace_back(std::move(queue.front()));
      queue.pop_front();
    }
    count -= to_copy;
    current_queue_depth_ -= to_copy;

    while (queues_[max_priority_].empty() && max_priority_ > 0) {
      --max_priority_;
    }
  }
  return cpp20::span<Frame>(outgoing_);
}

}  // namespace wlan::drivers::components
