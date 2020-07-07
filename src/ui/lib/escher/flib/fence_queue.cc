// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_queue.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace escher {

void FenceQueue::QueueTask(fit::function<void()> task, std::vector<zx::event> fences) {
  queue_.emplace_back(std::move(task), std::move(fences));
  ProcessQueue();
}

void FenceQueue::ProcessQueue() {
  if (queue_.empty() || fence_listener_) {
    // The queue is either already being processed or there is nothing in the queue to process.
    return;
  }

  // Handle the next task on the queue.
  fence_listener_.emplace(std::move(queue_.front().second));
  queue_.front().second.clear();

  fence_listener_->WaitReadyAsync([weak = weak_from_this()] {
    if (auto locked = weak.lock()) {
      // Execute task.
      locked->queue_.front().first();
    }

    // The FenceQueue may have been destroyed in the task. Retry the lock to avoid any weirdness.
    if (auto locked = weak.lock()) {
      // Keep going until all queued presents have been scheduled.
      locked->queue_.pop_front();
      locked->fence_listener_.reset();

      // After we delete the fence listener, this closure may be deleted. In that case, we should no
      // longer access any closed variables.
      locked->ProcessQueue();
    }
  });
}

}  // namespace escher
