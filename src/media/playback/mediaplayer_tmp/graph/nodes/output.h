// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_OUTPUT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_OUTPUT_H_

#include "src/media/playback/mediaplayer_tmp/graph/packet.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_config.h"

namespace media_player {

class Node;
class Input;

// Represents a node's connector to an adjacent downstream node.
class Output {
 public:
  Output(Node* node, size_t index);

  Output(Output&& output);

  ~Output();

  // The node of which this output is a part.
  Node* node() const { return node_; }

  // The index of this output with respect to the node.
  size_t index() const { return index_; }

  // The input to which this output is connected.
  Input* mate() const { return mate_; }

  // Establishes a connection.
  void Connect(Input* input);

  // Breaks a connection. Called only by the engine.
  void Disconnect() { mate_ = nullptr; }

  // Determines whether the output is connected to an input.
  bool connected() const { return mate_; }

  // Need for a packet signalled from downstream, or false if the downstream
  // input is currently holding a packet.
  bool needs_packet() const;

  // Supplies a packet to mate. Called only by Node::Update
  // implementations.
  void SupplyPacket(PacketPtr packet) const;

  // Returns a reference to the payload configuration.
  PayloadConfig& payload_config() { return payload_config_; }
  const PayloadConfig& payload_config() const { return payload_config_; }

 private:
  Node* node_;
  size_t index_;
  Input* mate_ = nullptr;
  PayloadConfig payload_config_;
  zx::handle bti_handle_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_OUTPUT_H_
