// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>

#include <queue>

#include "src/connectivity/overnet/lib/routing/router.h"

namespace overnet {

// Manages a queue of outgoing messages, packing them into packets on request.
class PacketStuffer {
 public:
  PacketStuffer(NodeId my_node_id, NodeId peer_node_id);

  // Forward a message.
  // Returns true if this is the first queued message.
  [[nodiscard]] bool Forward(Message message);

  void DropPendingMessages();
  bool HasPendingMessages() const;

  Slice BuildPacket(LazySliceArgs args);
  Status ParseAndForwardTo(TimeStamp received, Slice packet,
                           Router* router) const;

 private:
  const NodeId my_node_id_;
  const NodeId peer_node_id_;
  std::queue<Message> outgoing_;
  Optional<MessageWithPayload> stashed_;

  // data for a send
  std::vector<Slice> send_slices_;
};

}  // namespace overnet
