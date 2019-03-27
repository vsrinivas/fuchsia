// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_INPUT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_INPUT_H_

#include <atomic>
#include "src/media/playback/mediaplayer_tmp/graph/packet.h"
#include "src/media/playback/mediaplayer_tmp/graph/payloads/payload_manager.h"

namespace media_player {

class Node;
class Output;

// Represents a node's connector to an adjacent upstream node.
class Input {
 public:
  Input(Node* node, size_t index);

  Input(Input&& input);

  ~Input();

  // The node of which this input is a part.
  Node* node() const { return node_; }

  // The index of this input with respect to the node.
  size_t index() const { return index_; }

  // The output to which this input is connected.
  Output* mate() const { return mate_; }

  // Establishes a connection.
  void Connect(Output* output);

  // Breaks a connection. Called only by the engine.
  void Disconnect() {
    mate_ = nullptr;
    payload_manager_.OnDisconnect();
  }

  // Determines whether the input is connected to an output.
  bool connected() const { return mate_; }

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
  // node.
  void RequestPacket();

  // Flushes retained media.
  void Flush();

  // Returns a reference to the payload configuration.
  PayloadConfig& payload_config() { return payload_config_; }
  const PayloadConfig& payload_config() const { return payload_config_; }

  // Returns a reference to the buffer manager for this input.
  PayloadManager& payload_manager() { return payload_manager_; }
  const PayloadManager& payload_manager() const { return payload_manager_; }

 private:
  enum class State { kNeedsPacket, kRefusesPacket, kHasPacket };

  Node* node_;
  size_t index_;
  Output* mate_ = nullptr;
  PacketPtr packet_;
  std::atomic<State> state_;
  PayloadConfig payload_config_;
  PayloadManager payload_manager_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_GRAPH_NODES_INPUT_H_
