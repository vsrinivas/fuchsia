// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/models/demand.h"
#include "garnet/bin/media/framework/models/node.h"
#include "garnet/bin/media/framework/models/stage.h"
#include "garnet/bin/media/framework/packet.h"
#include "garnet/bin/media/framework/payload_allocator.h"

namespace media {

// Stage for |ActiveSink|.
class ActiveSinkStage : public Stage {
 public:
  virtual void SetDemand(Demand demand) = 0;
};

// Sink that consumes packets asynchronously.
class ActiveSink : public Node<ActiveSinkStage> {
 public:
  virtual ~ActiveSink() {}

  // Flushes media state. |hold_frame| indicates whether a video renderer
  // should hold (and display) the newest frame.
  virtual void Flush(bool hold_frame){};

  // An allocator that must be used for supplied packets or nullptr if there's
  // no such requirement.
  virtual std::shared_ptr<PayloadAllocator> allocator() = 0;

  // Supplies a packet to the sink, returning the new demand for the input.
  virtual Demand SupplyPacket(PacketPtr packet) = 0;
};

}  // namespace media
