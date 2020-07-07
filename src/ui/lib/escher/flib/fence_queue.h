// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLIB_FENCE_QUEUE_H_
#define SRC_UI_LIB_ESCHER_FLIB_FENCE_QUEUE_H_

#include <lib/fit/function.h>

#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "src/ui/lib/escher/flib/fence_set_listener.h"

namespace escher {

// Class that creates a queue of tasks which are handled in insertion order as each of their sets of
// acquire fences are signalled.
// On deletion any tasks still in the queue will be cancelled even if their fences have been
// signalled.
// Must be managed by a std::shared_ptr.
class FenceQueue final : public std::enable_shared_from_this<FenceQueue> {
 public:
  void QueueTask(fit::function<void()> task, std::vector<zx::event> fences);

 private:
  void ProcessQueue();

  std::deque<std::pair</*task*/ fit::function<void()>, /*fences*/ std::vector<zx::event>>> queue_;
  std::optional<escher::FenceSetListener> fence_listener_ = std::nullopt;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLIB_FENCE_QUEUE_H_
