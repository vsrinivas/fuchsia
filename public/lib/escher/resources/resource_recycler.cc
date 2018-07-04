// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/resources/resource_recycler.h"

#include "lib/escher/escher.h"

namespace escher {

ResourceRecycler::ResourceRecycler(EscherWeakPtr escher)
    : ResourceManager(std::move(escher)) {
  // Register ourselves for sequence number updates. Register() is defined in
  // our superclass CommandBufferSequenceListener.
  Register(this->escher()->command_buffer_sequencer());
}

ResourceRecycler::~ResourceRecycler() {
  FXL_DCHECK(unused_resources_.empty());
  // Unregister ourselves. Unregister() is defined in our superclass
  // CommandBufferSequenceListener.
  Unregister(escher()->command_buffer_sequencer());
}

void ResourceRecycler::OnReceiveOwnable(std::unique_ptr<Resource> resource) {
  if (resource->sequence_number() <= last_finished_sequence_number_) {
    // Recycle immediately.
    RecycleResource(std::move(resource));
  } else {
    // Defer recycling.
    unused_resources_[resource.get()] = std::move(resource);
  }
}

void ResourceRecycler::OnCommandBufferFinished(uint64_t sequence_number) {
  FXL_DCHECK(sequence_number > last_finished_sequence_number_);
  last_finished_sequence_number_ = sequence_number;

  // The sequence number allows us to find all unused resources that are no
  // longer referenced by a pending command-buffer; destroy these.
  auto it = unused_resources_.begin();
  while (it != unused_resources_.end()) {
    if (it->second->sequence_number() <= last_finished_sequence_number_) {
      RecycleResource(std::move(it->second));
      it = unused_resources_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace escher
