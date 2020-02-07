// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/release_fence_signaller.h"

namespace escher {

ReleaseFenceSignaller::ReleaseFenceSignaller(
    escher::impl::CommandBufferSequencer* command_buffer_sequencer)
    : command_buffer_sequencer_(command_buffer_sequencer) {
  // Register ourselves for sequence number updates.
  // Nullable for test.
  if (command_buffer_sequencer_) {
    command_buffer_sequencer_->AddListener(this);
  }
}

ReleaseFenceSignaller::~ReleaseFenceSignaller() {
  // Unregister ourselves.
  // Nullable for test.
  if (command_buffer_sequencer_) {
    command_buffer_sequencer_->RemoveListener(this);
  }
};

void ReleaseFenceSignaller::AddVulkanReleaseFence(zx::event fence) {
  // TODO: Submit a command buffer with the vulkan fence as a semaphore
  FXL_LOG(ERROR) << "Vulkan Release Fences not yet supported.";
  FXL_DCHECK(false);
}

void ReleaseFenceSignaller::AddVulkanReleaseFences(fidl::VectorPtr<zx::event> fences) {
  // TODO: Submit a command buffer with the vulkan fence as a semaphore
  FXL_LOG(ERROR) << "Vulkan Release Fences not yet supported.";
  FXL_DCHECK(false);
}

void ReleaseFenceSignaller::AddCPUReleaseFence(zx::event fence) {
  uint64_t latest_sequence_number = command_buffer_sequencer_->latest_sequence_number();

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

// Must be called on the same thread that we're submitting frames to Escher.
void ReleaseFenceSignaller::AddCPUReleaseFences(fidl::VectorPtr<zx::event> fences) {
  for (size_t i = 0; i < fences->size(); ++i) {
    AddCPUReleaseFence(std::move(fences->at(i)));
  }
}

void ReleaseFenceSignaller::OnCommandBufferFinished(uint64_t sequence_number) {
  // Iterate through the pending fences until you hit something that is
  // greater than |sequence_number|.
  last_finished_sequence_number_ = sequence_number;

  while (!pending_fences_.empty() && pending_fences_.front().sequence_number <= sequence_number) {
    pending_fences_.front().fence.signal(0u, kFenceSignalled);
    pending_fences_.pop();
  }
};

}  // namespace escher
