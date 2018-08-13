// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_INPUT_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_INPUT_H_

#include <atomic>

#include "garnet/bin/mediaplayer/framework/packet.h"

namespace media_player {

class StageImpl;
class Engine;
class Output;

// Represents a stage's connector to an adjacent upstream stage.
class Input {
 public:
  Input(StageImpl* stage, size_t index);

  Input(Input&& input);

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

  // Indicates current need for a packet. Called only by the upstream |Output|.
  bool needs_packet() const;

  // Updates packet. Called only by the upstream |Output|.
  void PutPacket(PacketPtr packet);

  // A packet supplied from upstream.
  const PacketPtr& packet() const { return packet_; }

  // Takes ownership of the packet supplied from upstream and requests another
  // if |request_another| is true.
  PacketPtr TakePacket(bool request_another);

  // Requests a packet if |packet()| is empty. Called only by the downstream
  // stage.
  void RequestPacket();

  // Flushes retained media.
  void Flush();

 private:
  enum class State { kNeedsPacket, kRefusesPacket, kHasPacket };

  StageImpl* stage_;
  size_t index_;
  Output* mate_ = nullptr;
  bool prepared_ = false;
  PacketPtr packet_;
  std::atomic<State> state_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_STAGES_INPUT_H_
