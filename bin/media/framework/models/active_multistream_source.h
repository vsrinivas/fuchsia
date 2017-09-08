// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/models/node.h"
#include "garnet/bin/media/framework/models/stage.h"
#include "garnet/bin/media/framework/packet.h"

namespace media {

// Stage for |ActiveMultistreamSource|.
class ActiveMultistreamSourceStage : public Stage {
 public:
  // Supplies a packet for the indicated output.
  virtual void SupplyPacket(size_t output_index, PacketPtr packet) = 0;
};

// Asynchronous source of packets for multiple streams.
class ActiveMultistreamSource : public Node<ActiveMultistreamSourceStage> {
 public:
  virtual ~ActiveMultistreamSource() {}

  // Flushes media state.
  virtual void Flush(){};

  // TODO(dalesat): Support dynamic output creation.

  // Returns the number of streams the source produces.
  virtual size_t stream_count() const = 0;

  // Requests a packet from the source to be supplied asynchronously via
  // the supply callback.
  virtual void RequestPacket() = 0;
};

}  // namespace media
