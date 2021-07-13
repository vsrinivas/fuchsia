// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_PROGRESS_H
#define GPU_PROGRESS_H

#include <chrono>

#include "sequencer.h"

class GpuProgress {
 public:
  void Submitted(uint32_t sequence_number, std::chrono::steady_clock::time_point time);

  void Completed(uint32_t sequence_number, std::chrono::steady_clock::time_point time);

  std::chrono::steady_clock::duration GetHangcheckTimeout(
      uint64_t max_completion_time_ms, std::chrono::steady_clock::time_point now);

  uint32_t last_submitted_sequence_number() const { return last_submitted_sequence_number_; }

  uint32_t last_completed_sequence_number() const { return last_completed_sequence_number_; }

 private:
  uint32_t last_submitted_sequence_number_ = Sequencer::kInvalidSequenceNumber;
  uint32_t last_completed_sequence_number_ = Sequencer::kInvalidSequenceNumber;
  std::chrono::steady_clock::time_point hangcheck_start_time_ =
      std::chrono::steady_clock::time_point::max();
};

#endif  // GPU_PROGRESS_H
