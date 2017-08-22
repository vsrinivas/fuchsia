// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/demand.h"
#include "apps/media/src/framework/models/node.h"
#include "apps/media/src/framework/models/stage.h"
#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"

namespace media {

// Stage for |ActiveSource|.
class ActiveSourceStage : public Stage {
 public:
  virtual void SupplyPacket(PacketPtr packet) = 0;
};

// Source that produces packets asynchronously.
class ActiveSource : public Node<ActiveSourceStage> {
 public:
  virtual ~ActiveSource() {}

  // Flushes media state.
  virtual void Flush(){};

  // Whether the source can accept an allocator.
  virtual bool can_accept_allocator() const = 0;

  // Sets the allocator for the source.
  virtual void set_allocator(PayloadAllocator* allocator) = 0;

  // Sets the demand signalled from downstream.
  virtual void SetDownstreamDemand(Demand demand) = 0;
};

}  // namespace media
