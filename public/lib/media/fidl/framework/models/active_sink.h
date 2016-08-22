// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MODELS_ACTIVE_SINK_H_
#define SERVICES_MEDIA_FRAMEWORK_MODELS_ACTIVE_SINK_H_

#include "services/media/framework/models/demand.h"
#include "services/media/framework/models/part.h"
#include "services/media/framework/packet.h"
#include "services/media/framework/payload_allocator.h"

namespace mojo {
namespace media {

// Sink that consumes packets asynchronously.
class ActiveSink : public Part {
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
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_MODELS_ACTIVE_SINK_H_
