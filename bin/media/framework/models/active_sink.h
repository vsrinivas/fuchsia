// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/demand.h"
#include "apps/media/src/framework/models/node.h"
#include "apps/media/src/framework/packet.h"
#include "apps/media/src/framework/payload_allocator.h"

namespace media {

// Sink that consumes packets asynchronously.
class ActiveSink : public Node {
 public:
  using DemandCallback = std::function<void(Demand demand)>;

  ~ActiveSink() override {}

  // An allocator that must be used for supplied packets or nullptr if there's
  // no such requirement.
  virtual PayloadAllocator* allocator() = 0;

  // Sets the callback that signals demand asynchronously.
  virtual void SetDemandCallback(const DemandCallback& demand_callback) = 0;

  // Supplies a packet to the sink, returning the new demand for the input.
  virtual Demand SupplyPacket(PacketPtr packet) = 0;
};

}  // namespace media
