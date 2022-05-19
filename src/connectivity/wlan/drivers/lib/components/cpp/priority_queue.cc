// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/lib/components/cpp/include/wlan/drivers/components/priority_queue.h"

#include <optional>

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

void PriorityQueue::pop(size_t count, uint8_t allowed_priorities, FrameContainer* frames) {
  while (count > 0) {
    uint8_t priority = max_priority_;
    while ((allowed_priorities & (1 << priority)) == 0 || queues_[priority].empty()) {
      if (priority == 0) {
        return;
      }
      --priority;
    }

    auto& queue = queues_[priority];

    const size_t to_copy = std::min<size_t>(count, queue.size());
    for (size_t i = 0; i < to_copy; ++i) {
      frames->emplace_back(std::move(queue.front()));
      queue.pop_front();
    }
    count -= to_copy;
    current_queue_depth_ -= to_copy;

    while (queues_[max_priority_].empty() && max_priority_ > 0) {
      --max_priority_;
    }
  }
}

// Pop any number of frames that match |predicate|. Frames will be evaluated, popped and returned in
// priority order. |predicate| will be called for each frame and if |predicate| returns true then
// the frame will be poppped. This is a slow operation and should not be used in a regular data
// path, only for rare cases where specific frames need to be popped or removed. Note that just as
// for pop the frames returned from this method must be consumed before the next call to pop or
// pop_if. See pop for more details.
void PriorityQueue::pop_if(std::function<bool(const Frame&)>&& predicate, FrameContainer* frames) {
  for (uint8_t priority = max_priority_; priority <= max_priority_; --priority) {
    auto& queue = queues_[priority];
    std::optional<int64_t> start_erase;
    for (int64_t i = 0; i < static_cast<int64_t>(queue.size()); ++i) {
      if (predicate(queue[i])) {
        frames->emplace_back(std::move(queue[i]));
        if (!start_erase.has_value()) {
          // We are starting a new erase sequence.
          start_erase = i;
        }
      } else if (start_erase.has_value()) {
        // We've ended an erase sequence, erase all the way from start_erase up to (but not
        // including) i.
        queue.erase(queue.begin() + start_erase.value(), queue.begin() + i);
        // Reduce the total size of the priority queue.
        current_queue_depth_ -= i - start_erase.value();
        // Start erase now points to the same frame as i used to point to. Since we already
        // evaluated the frame i used to point to we set i to start_erase so that it will be
        // increased in the next loop iteration.
        i = start_erase.value();
        start_erase.reset();
      }
    }
    if (start_erase.has_value()) {
      // Reduce the total size of the priority queue, do this before the erase since we need the
      // size of the queue before it's being reduced.
      current_queue_depth_ -= queue.size() - start_erase.value();
      // An erase sequence was started but never finished, erase everything until the end.
      queue.erase(queue.begin() + start_erase.value(), queue.end());
    }
    if (queue.empty() && priority == max_priority_ && max_priority_ > 0) {
      --max_priority_;
    }
  }
}
}  // namespace wlan::drivers::components
