// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <queue>

#include <mx/event.h>
#include "escher/impl/command_buffer_sequencer.h"
#include "lib/ftl/logging.h"

#include "apps/mozart/src/scene_manager/fence.h"

namespace scene_manager {

// Signals a fence when all CommandBuffers started before the time of the
// fence's submission are finished. Used to ensure it is safe to release
// resources.
class ReleaseFenceSignaller
    : public escher::impl::CommandBufferSequencerListener {
 public:
  explicit ReleaseFenceSignaller(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  ~ReleaseFenceSignaller();

  // Must be called on the same thread that we're submitting frames to Escher.
  void AddVulkanReleaseFence(mx::event fence);

  // Must be called on the same thread that we're submitting frames to Escher.
  virtual void AddCPUReleaseFence(mx::event fence);

 private:
  // The sequence number for the most recently finished CommandBuffer.
  uint64_t last_finished_sequence_number_ = 0;

  // Implement impl::CommandBufferSequenceListener::CommandBufferFinished().
  // Signals any fences that correspond to a CommandBuffer with a sequence
  // numbers equal to or less than |sequence_number|.
  void OnCommandBufferFinished(uint64_t sequence_number) override;

  // A fence along with the sequence number it is waiting for before it will be
  // signalled.
  struct FenceWithSequenceNumber {
    uint64_t sequence_number;
    mx::event fence;
  };

  // Queue of fences we need to signal along with their corresponding sequence
  // numbers. The sequence numbers must be in non-decreasing order.
  std::queue<FenceWithSequenceNumber> pending_fences_;

  // Used to query for last generated sequence number, corresponding to the most
  // recently submitted CommandBuffer.
  escher::impl::CommandBufferSequencer* command_buffer_sequencer_;
};

}  // namespace scene_manager
