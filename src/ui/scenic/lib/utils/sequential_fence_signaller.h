// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_SEQUENTIAL_FENCE_SIGNALLER_H_
#define SRC_UI_SCENIC_LIB_UTILS_SEQUENTIAL_FENCE_SIGNALLER_H_

#include <lib/zx/event.h>

#include <deque>
#include <queue>

#include "lib/fidl/cpp/vector.h"

namespace utils {

// Associates fences with sequence numbers and signals all fences up to and including some sequence
// number.
class SequentialFenceSignaller final {
 public:
  SequentialFenceSignaller() = default;
  ~SequentialFenceSignaller() = default;

  // Add fence and associate with a sequence number.
  // Not thread safe. Must be called from the same thread that calls SignalFencesUpTo().
  void AddFence(zx::event fence, uint64_t sequence_number);

  // Add fences and associate with a sequence number.
  // Not thread safe. Must be called from the same thread that calls SignalFencesUpTo().
  void AddFences(fidl::VectorPtr<zx::event> fences, uint64_t sequence_number);

  // Releases all fences up to and including |sequence_number|.
  void SignalFencesUpToAndIncluding(uint64_t sequence_number);

 private:
  uint64_t first_unfinished_sequence_number_ = 0;

  // A fence along with the sequence number it is waiting for before it will be signalled.
  struct FenceWithSequenceNumber {
    uint64_t sequence_number;
    zx::event fence;

    bool operator>(const FenceWithSequenceNumber& other) const {
      return sequence_number > other.sequence_number;
    }
  };

  // Priority queue of FenceWithSequenceNumber ordered by sequence_number. Lowest on top.
  std::priority_queue<FenceWithSequenceNumber, std::deque<FenceWithSequenceNumber>,
                      std::greater<FenceWithSequenceNumber>>
      pending_fences_;
};

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_SEQUENTIAL_FENCE_SIGNALLER_H_
