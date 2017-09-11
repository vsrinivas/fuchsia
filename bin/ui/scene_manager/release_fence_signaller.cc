// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/release_fence_signaller.h"

namespace scene_manager {

ReleaseFenceSignaller::ReleaseFenceSignaller(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : command_buffer_sequencer_(command_buffer_sequencer) {
  // Register ourselves for sequence number updates. Register() is defined in
  // our superclass CommandBufferSequenceListener.
  Register(command_buffer_sequencer_);
}

ReleaseFenceSignaller::~ReleaseFenceSignaller() {
  // Unregister ourselves. Unregister() is defined in our superclass
  // CommandBufferSequenceListener.
  Unregister(command_buffer_sequencer_);
};

void ReleaseFenceSignaller::AddVulkanReleaseFence(mx::event fence) {
  // TODO: Submit a command buffer with the vulkan fence as a semaphore
  FXL_LOG(ERROR) << "Vulkan Release Fences not yet supported.";
  FXL_DCHECK(false);
}

void ReleaseFenceSignaller::AddCPUReleaseFence(mx::event fence) {
  uint64_t latest_sequence_number =
      command_buffer_sequencer_->latest_sequence_number();

  if (latest_sequence_number > last_finished_sequence_number_) {
    pending_fences_.push({latest_sequence_number, std::move(fence)});
  } else if (latest_sequence_number == last_finished_sequence_number_) {
    // Signal the fence immediately if its sequence number has already been
    // marked finished.
    fence.signal(0u, kFenceSignalled);
  } else {
    FXL_CHECK(false) << "ReleaseFenceSignaller::AddCPUReleaseFence: sequence "
                        "numbers are in an invalid state";
  }
}

void ReleaseFenceSignaller::OnCommandBufferFinished(uint64_t sequence_number) {
  // Iterate through the pending fences until you hit something that is
  // greater than |sequence_number|.
  last_finished_sequence_number_ = sequence_number;

  while (!pending_fences_.empty() &&
         pending_fences_.front().sequence_number <= sequence_number) {
    pending_fences_.front().fence.signal(0u, kFenceSignalled);
    pending_fences_.pop();
  }
};

}  // namespace scene_manager
