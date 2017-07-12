// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/demand.h"
#include "apps/media/src/framework/models/node.h"
#include "apps/media/src/framework/packet.h"

namespace media {

// Host for ActiveMultistreamSink.
class ActiveMultistreamSinkHost {
 public:
  virtual ~ActiveMultistreamSinkHost() {}

  // TODO(dalesat): Revisit allocation semantics.

  // Allocates an input and returns its index.
  virtual size_t AllocateInput() = 0;

  // Releases a previously-allocated input and returns the container size
  // required to hold the remaining inputs (i.e. max input index + 1). The
  // return value can be used to resize the caller's input container.
  virtual size_t ReleaseInput(size_t index) = 0;

  // Updates demand for the specified input.
  virtual void UpdateDemand(size_t input_index, Demand demand) = 0;
};

// Synchronous sink of packets for multiple streams.
class ActiveMultistreamSink : public Node {
 public:
  ~ActiveMultistreamSink() override {}

  // Sets the host callback interface.
  virtual void SetHost(ActiveMultistreamSinkHost* host) = 0;

  // Supplies a packet to the sink, returning the new demand for the input.
  virtual Demand SupplyPacket(size_t input_index, PacketPtr packet) = 0;
};

}  // namespace media
