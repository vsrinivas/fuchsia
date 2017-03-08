// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_PROGRESS_H
#define GPU_PROGRESS_H

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "sequencer.h"
#include <chrono>

class GpuProgress {
public:
    void Submitted(uint32_t sequence_number)
    {
        DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
        if (sequence_number != last_submitted_sequence_number_) {
            DLOG("Submitted 0x%x", sequence_number);
            DASSERT(sequence_number > last_submitted_sequence_number_);

            if (last_submitted_sequence_number_ == last_completed_sequence_number_) {
                hangcheck_time_start_ = std::chrono::high_resolution_clock::now();
            }

            last_submitted_sequence_number_ = sequence_number;
        }
    }

    void Completed(uint32_t sequence_number)
    {
        DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
        if (sequence_number != last_completed_sequence_number_) {
            DLOG("Completed 0x%x", sequence_number);
            DASSERT(sequence_number > last_completed_sequence_number_);
            last_completed_sequence_number_ = sequence_number;
            hangcheck_time_start_ = std::chrono::high_resolution_clock::now();
        } else {
            DLOG("completed 0x%x AGAIN\n", sequence_number);
        }

        // Handle initial condition - init batch isn't submitted as a command buffer.
        if (last_submitted_sequence_number_ == Sequencer::kInvalidSequenceNumber)
            last_submitted_sequence_number_ = last_completed_sequence_number_;
    }

    bool work_outstanding() { return last_submitted_sequence_number_ > last_completed_sequence_number_; }

    uint32_t last_submitted_sequence_number() { return last_submitted_sequence_number_; }

    std::chrono::high_resolution_clock::time_point hangcheck_time_start() { return hangcheck_time_start_; }

private:
    uint32_t last_submitted_sequence_number_ = Sequencer::kInvalidSequenceNumber;
    uint32_t last_completed_sequence_number_ = Sequencer::kInvalidSequenceNumber;
    std::chrono::high_resolution_clock::time_point hangcheck_time_start_{};
};

#endif // GPU_PROGRESS_H
