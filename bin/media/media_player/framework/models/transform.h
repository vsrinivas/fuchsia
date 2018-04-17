// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_TRANSFORM_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_TRANSFORM_H_

#include "garnet/bin/media/media_player/framework/models/node.h"
#include "garnet/bin/media/media_player/framework/models/stage.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/payload_allocator.h"

namespace media_player {

// Stage for |Transform|.
class TransformStage : public Stage {};

// Synchronous packet transform.
class Transform : public Node<TransformStage> {
 public:
  ~Transform() override {}

  // Flushes media state.
  virtual void Flush(){};

  // Processes a packet. Returns true to indicate the transform is done
  // processing the input packet. Returns false to indicate the input
  // packet should be processed again. new_input indicates whether the input
  // packet is new (true) or is being processed again (false). An output packet
  // may or may not be generated for any given invocation of this method.
  virtual bool TransformPacket(
      const PacketPtr& input,
      bool new_input,
      const std::shared_ptr<PayloadAllocator>& allocator,
      PacketPtr* output) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_TRANSFORM_H_
