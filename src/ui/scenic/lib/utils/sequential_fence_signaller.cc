// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/sequential_fence_signaller.h"

#include "src/lib/fxl/logging.h"

namespace utils {

void SequentialFenceSignaller::AddFence(zx::event fence, uint64_t sequence_number) {
  if (sequence_number >= first_unfinished_sequence_number_) {
    pending_fences_.push({sequence_number, std::move(fence)});
  } else {
    // Signal the fence immediately if its sequence number has already been
    // marked finished.
    fence.signal(0u, ZX_EVENT_SIGNALED);
  }
}

void SequentialFenceSignaller::AddFences(fidl::VectorPtr<zx::event> fences,
                                         uint64_t sequence_number) {
  for (size_t i = 0; i < fences->size(); ++i) {
    AddFence(std::move(fences->at(i)), sequence_number);
  }
}

void SequentialFenceSignaller::SignalFencesUpToAndIncluding(uint64_t sequence_number) {
  // Iterate through the pending fences until you hit something that is
  // greater than |sequence_number|.
  while (!pending_fences_.empty() && pending_fences_.top().sequence_number <= sequence_number) {
    pending_fences_.top().fence.signal(0u, ZX_EVENT_SIGNALED);
    pending_fences_.pop();
  }

  first_unfinished_sequence_number_ =
      std::max(sequence_number + 1, first_unfinished_sequence_number_);
};

}  // namespace utils
