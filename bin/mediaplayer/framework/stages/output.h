// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_OUTPUT_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_OUTPUT_H_

#include "garnet/bin/mediaplayer/framework/packet.h"
#include "garnet/bin/mediaplayer/framework/payload_allocator.h"

namespace media_player {

class StageImpl;
class Engine;
class Input;

// Represents a stage's connector to an adjacent downstream stage.
class Output {
 public:
  Output(StageImpl* stage, size_t index);

  ~Output();

  // The stage of which this output is a part.
  StageImpl* stage() const { return stage_; }

  // The index of this output with respect to the stage.
  size_t index() const { return index_; }

  // The input to which this output is connected.
  Input* mate() const { return mate_; }

  // Establishes a connection.
  void Connect(Input* input);

  // Breaks a connection. Called only by the engine.
  void Disconnect() { mate_ = nullptr; }

  // Determines whether the output is connected to an input.
  bool connected() const { return mate_; }

  // Sets the allocator the output must use to copy the payload of output
  // packets. This is used when the connected input insists that a specific
  // allocator be used, but the stage can't use it.
  void SetCopyAllocator(std::shared_ptr<PayloadAllocator> copy_allocator);

  // Need for a packet signalled from downstream, or false if the downstream
  // input is currently holding a packet.
  bool needs_packet() const;

  // Supplies a packet to mate. Called only by StageImpl::Update
  // implementations.
  void SupplyPacket(PacketPtr packet) const;

 private:
  StageImpl* stage_;
  size_t index_;
  Input* mate_ = nullptr;
  std::shared_ptr<PayloadAllocator> copy_allocator_ = nullptr;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_OUTPUT_H_
