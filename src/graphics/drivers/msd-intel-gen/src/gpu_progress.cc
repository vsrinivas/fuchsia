// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_progress.h"

#include "magma_util/dlog.h"

void GpuProgress::Submitted(uint32_t sequence_number, std::chrono::steady_clock::time_point time) {
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

void GpuProgress::Completed(uint32_t sequence_number, std::chrono::steady_clock::time_point time) {
  DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
  if (sequence_number != last_completed_sequence_number_) {
    DLOG("Completed 0x%x", sequence_number);
    DASSERT(sequence_number > last_completed_sequence_number_);
    last_completed_sequence_number_ = sequence_number;
  } else {
    DLOG("completed 0x%x AGAIN", sequence_number);
  }

  // Handle initial condition - init batch isn't submitted as a command buffer.
  if (last_submitted_sequence_number_ == Sequencer::kInvalidSequenceNumber)
    last_submitted_sequence_number_ = last_completed_sequence_number_;

  if (last_completed_sequence_number_ == last_submitted_sequence_number_) {
    // Going idle.
    hangcheck_start_time_ = std::chrono::steady_clock::time_point::max();
  } else {
    // Starting more work.
    hangcheck_start_time_ = time;
  }
}

std::chrono::steady_clock::duration GpuProgress::GetHangcheckTimeout(
    uint64_t max_completion_time_ms, std::chrono::steady_clock::time_point now) {
  if (hangcheck_start_time_ == std::chrono::steady_clock::time_point::max())
    return std::chrono::steady_clock::duration::max();
  return hangcheck_start_time_ + std::chrono::milliseconds(max_completion_time_ms) - now;
}

void GpuProgress::Reset() {
  DLOG("Resetting to last submitted sequence 0x%x", last_submitted_sequence_number());
  Completed(last_submitted_sequence_number(), std::chrono::steady_clock::now());
}
