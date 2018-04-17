// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_ACTIVE_SOURCE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_ACTIVE_SOURCE_H_

#include "garnet/bin/media/media_player/framework/models/demand.h"
#include "garnet/bin/media/media_player/framework/models/node.h"
#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"

namespace media_player {

// Stage for |ActiveSource|.
class ActiveSourceStage : public Stage {
 public:
  ~ActiveSourceStage() override {}

  virtual void SupplyPacket(PacketPtr packet) = 0;
};

// Source that produces packets asynchronously.
class ActiveSource : public Node<ActiveSourceStage> {
 public:
  ~ActiveSource() override {}

  // Flushes media state.
  virtual void Flush(){};

  // Whether the source can accept an allocator.
  virtual bool can_accept_allocator() const = 0;

  // Sets the allocator for the source.
  virtual void set_allocator(std::shared_ptr<PayloadAllocator> allocator) = 0;

  // Sets the demand signalled from downstream.
  virtual void SetDownstreamDemand(Demand demand) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_ACTIVE_SOURCE_H_
