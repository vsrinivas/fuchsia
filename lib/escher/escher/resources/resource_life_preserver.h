// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_set>

#include "escher/impl/command_buffer_sequencer.h"
#include "escher/resources/resource_core.h"

namespace escher {

// Simple manager that keeps resources alive until they are no longer referenced
// by a pending command-buffer, then destroys them.
class ResourceLifePreserver : public ResourceCoreManager,
                              public impl::CommandBufferSequencerListener {
 public:
  explicit ResourceLifePreserver(const VulkanContext& context);
  virtual ~ResourceLifePreserver();

  void CommandBufferFinished(uint64_t sequence_number) override;

 private:
  void ReceiveResourceCore(std::unique_ptr<ResourceCore> core) override;

  uint64_t last_finished_sequence_number_ = 0;
  std::unordered_set<std::unique_ptr<ResourceCore>> unused_resources_;
};

}  // namespace escher
