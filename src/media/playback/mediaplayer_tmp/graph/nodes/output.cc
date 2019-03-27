// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/graph/nodes/output.h"

#include "src/media/playback/mediaplayer_tmp/graph/nodes/input.h"
#include "src/media/playback/mediaplayer_tmp/graph/nodes/node.h"

namespace media_player {

Output::Output(Node* node, size_t index) : node_(node), index_(index) {}

Output::Output(Output&& output)
    : node_(output.node()),
      index_(output.index()),
      payload_config_(output.payload_config()),
      bti_handle_(std::move(output.bti_handle_)) {
  // We can't move an output that's connected.
  // TODO(dalesat): Make |Output| non-movable.
  FXL_DCHECK(output.mate() == nullptr);
}

Output::~Output() {}

void Output::Connect(Input* input) {
  FXL_DCHECK(input);
  FXL_DCHECK(!mate_);
  mate_ = input;

  if (payload_config_.mode_ != PayloadMode::kNotConfigured) {
    mate_->payload_manager().ApplyOutputConfiguration(payload_config_,
                                                      std::move(bti_handle_));
  }
}

bool Output::needs_packet() const {
  FXL_DCHECK(mate_);
  return mate_->needs_packet();
}

void Output::SupplyPacket(PacketPtr packet) const {
  FXL_DCHECK(packet);
  FXL_DCHECK(mate_);
  FXL_DCHECK(needs_packet());

  mate_->PutPacket(std::move(packet));
}

}  // namespace media_player
