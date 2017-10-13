// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/command_buffer_sequencer.h"

#include "lib/fxl/logging.h"

namespace escher {
namespace impl {

void CommandBufferSequencerListener::Register(
    CommandBufferSequencer* sequencer) {
  FXL_DCHECK(sequencer);
  sequencer->AddListener(this);
}

void CommandBufferSequencerListener::Unregister(
    CommandBufferSequencer* sequencer) {
  FXL_DCHECK(sequencer);
  sequencer->RemoveListener(this);
}

CommandBufferSequencer::~CommandBufferSequencer() {
  // Ensure clean shutdown.
  FXL_DCHECK(latest_sequence_number_ == last_finished_sequence_number_);
  FXL_DCHECK(listeners_.size() == 0);
}

uint64_t CommandBufferSequencer::GenerateNextCommandBufferSequenceNumber() {
  return ++latest_sequence_number_;
}

void CommandBufferSequencer::CommandBufferFinished(uint64_t sequence_number) {
  if (sequence_number != last_finished_sequence_number_ + 1) {
    // There is a gap.  Remember the just-finished sequence number so that we
    // can notify listeners once the gap is filled.
    out_of_sequence_numbers_.push_back(sequence_number);
    std::sort(out_of_sequence_numbers_.begin(), out_of_sequence_numbers_.end());
  } else {
    ++last_finished_sequence_number_;

    // If there were any buffers that were finished "out of sequence", the gap
    // between them and last_finished_sequence_number_ may now be filled.
    size_t removed = 0;
    for (size_t i = 0; i < out_of_sequence_numbers_.size(); ++i) {
      if (out_of_sequence_numbers_[i] == last_finished_sequence_number_ + 1) {
        ++last_finished_sequence_number_;
        ++removed;
      } else {
        // This works because we sort the list every time we add to
        // out_of_sequence_numbers_
        out_of_sequence_numbers_[i - removed] = out_of_sequence_numbers_[i];
      }
    }
    out_of_sequence_numbers_.resize(out_of_sequence_numbers_.size() - removed);

    // Notify listeners.
    for (auto& listener : listeners_) {
      listener->OnCommandBufferFinished(last_finished_sequence_number_);
    }
  }
}

void CommandBufferSequencer::AddListener(
    CommandBufferSequencerListener* listener) {
  if (listener) {
    FXL_DCHECK(std::find(listeners_.begin(), listeners_.end(), listener) ==
               listeners_.end());
    listeners_.push_back(listener);
  }
}

void CommandBufferSequencer::RemoveListener(
    CommandBufferSequencerListener* listener) {
  if (listener) {
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
  }
}

}  // namespace impl
}  // namespace escher
