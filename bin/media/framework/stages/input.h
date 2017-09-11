// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>

#include "garnet/bin/media/framework/models/demand.h"
#include "garnet/bin/media/framework/packet.h"

namespace media {

class StageImpl;
class Engine;
class Output;

// Represents a stage's connector to an adjacent upstream stage.
class Input {
 public:
  Input(StageImpl* stage, size_t index);

  ~Input();

  // The stage of which this input is a part.
  StageImpl* stage() const { return stage_; }

  // The index of this input with respect to the stage.
  size_t index() const { return index_; }

  // The output to which this input is connected.
  Output* mate() const { return mate_; }

  // Establishes a connection.
  void Connect(Output* output);

  // Breaks a connection. Called only by the engine.
  void Disconnect() {
    FXL_DCHECK(!prepared_);
    mate_ = nullptr;
  }

  // Determines whether the input is connected to an output.
  bool connected() const { return mate_; }

  // Determines if the input is prepared.
  bool prepared() const { return prepared_; }

  // Changes the prepared state of the input.
  void set_prepared(bool prepared) { prepared_ = prepared; }

  // Indicates current demand. Called only by the upstream |Output|.
  Demand demand() const;

  // Updates packet. Called only by the upstream |Output|.
  void PutPacket(PacketPtr packet);

  // A packet supplied from upstream.
  const PacketPtr& packet() { return packet_; }

  // Takes ownership of the packet supplied from upstream and sets the demand
  // to the indicated value.
  PacketPtr TakePacket(Demand demand);

  // Updates mate's demand if |packet()| is empty. Called only by the downstream
  // stage.
  void SetDemand(Demand demand);

  // Flushes retained media.
  void Flush();

 private:
  enum class State {
    kDemandsPacket,
    kAllowsPacket,
    kRefusesPacket,
    kHasPacket
  };

  StageImpl* stage_;
  size_t index_;
  Output* mate_ = nullptr;
  bool prepared_ = false;
  PacketPtr packet_;
  std::atomic<State> state_;
};

}  // namespace media
