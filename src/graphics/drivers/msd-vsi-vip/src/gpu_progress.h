// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_GPU_PROGRESS_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_GPU_PROGRESS_H_

#include <chrono>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "sequencer.h"

class GpuProgress {
 public:
  void Submitted(uint32_t sequence_number, std::chrono::steady_clock::time_point time) {
    DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
    if (sequence_number != last_submitted_sequence_number_) {
      DLOG("Submitted 0x%x", sequence_number);
      DASSERT(sequence_number > last_submitted_sequence_number_);
      if (last_submitted_sequence_number_ == last_completed_sequence_number_) {
        // Starting from idle.
        hangcheck_start_time_ = time;
      }
      last_submitted_sequence_number_ = sequence_number;
    }
  }

  void Completed(uint32_t sequence_number, std::chrono::steady_clock::time_point time) {
    DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
    if (sequence_number != last_completed_sequence_number_) {
      DLOG("Completed 0x%x", sequence_number);
      DASSERT(sequence_number > last_completed_sequence_number_);
      last_completed_sequence_number_ = sequence_number;
    } else {
      DLOG("completed 0x%x AGAIN", sequence_number);
    }
    if (last_completed_sequence_number_ == last_submitted_sequence_number_) {
      // Going idle.
      hangcheck_start_time_ = std::chrono::steady_clock::time_point::max();
    } else {
      // Starting more work.
      hangcheck_start_time_ = time;
    }
  }

  std::chrono::steady_clock::duration GetHangcheckTimeout(
      uint64_t max_completion_time_ms, std::chrono::steady_clock::time_point now) {
    if (hangcheck_start_time_ == std::chrono::steady_clock::time_point::max())
      return std::chrono::steady_clock::duration::max();
    return hangcheck_start_time_ + std::chrono::milliseconds(max_completion_time_ms) - now;
  }

  uint32_t last_submitted_sequence_number() const { return last_submitted_sequence_number_; }
  uint32_t last_completed_sequence_number() const { return last_completed_sequence_number_; }

 private:
  uint32_t last_submitted_sequence_number_ = Sequencer::kInvalidSequenceNumber;
  uint32_t last_completed_sequence_number_ = Sequencer::kInvalidSequenceNumber;
  std::chrono::steady_clock::time_point hangcheck_start_time_ =
      std::chrono::steady_clock::time_point::max();
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_GPU_PROGRESS_H_
