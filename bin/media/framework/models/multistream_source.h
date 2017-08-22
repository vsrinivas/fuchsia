// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/node.h"
#include "apps/media/src/framework/models/stage.h"
#include "apps/media/src/framework/packet.h"

namespace media {

// Stage for |MultistreamSource|.
class MultistreamSourceStage : public Stage {};

// Synchronous source of packets for multiple streams.
class MultistreamSource : public Node<MultistreamSourceStage> {
 public:
  virtual ~MultistreamSource() {}

  // Flushes media state.
  virtual void Flush(){};

  // Returns the number of streams the source produces.
  virtual size_t stream_count() const = 0;

  // Gets a packet for the stream indicated via stream_index_out. This call
  // should always produce a packet until end-of-stream. The caller is
  // responsible for releasing the packet.
  virtual PacketPtr PullPacket(size_t* stream_index_out) = 0;
};

}  // namespace media
