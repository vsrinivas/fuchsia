// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/src/framework/models/demand.h"
#include "apps/media/src/framework/packet.h"

namespace media {

class Stage;
class Engine;
class Output;

// Represents a stage's connector to an adjacent upstream stage.
class Input {
 public:
  Input(Stage* stage, size_t index);

  ~Input();

  // The stage of which this input is a part.
  Stage* stage() const { return stage_; }

  // The index of this input with respect to the stage.
  size_t index() const { return index_; }

  // The output to which this input is connected.
  Output* mate() const { return mate_; }

  // Establishes a connection.
  void Connect(Output* output);

  // Breaks a connection. Called only by the engine.
  void Disconnect() {
    FTL_DCHECK(!prepared_);
    mate_ = nullptr;
  }

  // Determines whether the input is connected to an output.
  bool connected() const { return mate_; }

  // Determines if the input is prepared.
  bool prepared() { return prepared_; }

  // Changes the prepared state of the input.
  void set_prepared(bool prepared) { prepared_ = prepared; }

  // A packet supplied from upstream.
  PacketPtr& packet_from_upstream() { return packet_from_upstream_; }

  // Updates mate's demand. Called only by Stage::Update implementations.
  void SetDemand(Demand demand) const;

  // Updates packet_from_upstream. Return value indicates whether the stage for
  // this input should be added to the supply backlog. Called only by
  // Output instances.
  bool SupplyPacketFromOutput(PacketPtr packet);

  // Flushes retained media.
  void Flush();

 private:
  Stage* stage_;
  size_t index_;
  Output* mate_ = nullptr;
  bool prepared_ = false;
  PacketPtr packet_from_upstream_;
};

}  // namespace media
