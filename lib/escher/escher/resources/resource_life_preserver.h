// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_set>

#include "escher/impl/command_buffer_sequencer.h"
#include "escher/resources/resource_manager.h"

namespace escher {

// Simple manager that keeps resources alive until they are no longer referenced
// by a pending command-buffer, then destroys them.  It does this by comparing
// the sequence numbers from a CommandBufferSequencer with the sequence numbers
// of resources that it is keeping alive.
class ResourceLifePreserver : public ResourceManager,
                              public impl::CommandBufferSequencerListener {
 public:
  explicit ResourceLifePreserver(const VulkanContext& context);
  virtual ~ResourceLifePreserver();

 private:
  // Implement impl::CommandBufferSequenceListener::CommandBufferFinished().
  // Checks whether it is safe to destroy any of |unused_resources_|.
  void CommandBufferFinished(uint64_t sequence_number) override;

  // Implement Owner::OnReceiveOwnable().  Destroys the resource immediately if
  // it is safe to do so.  Otherwise, adds the resource to a set of resources
  // to be destroyed later; see CommandBufferFinished().
  void OnReceiveOwnable(std::unique_ptr<Resource2> resource) override;

  uint64_t last_finished_sequence_number_ = 0;
  std::unordered_set<std::unique_ptr<Resource2>> unused_resources_;
};

}  // namespace escher
