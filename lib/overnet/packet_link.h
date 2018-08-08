// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include "packet_protocol.h"
#include "router.h"

namespace overnet {

class PacketLink : public Link, private PacketProtocol::PacketSender {
 public:
  PacketLink(Router* router, NodeId peer, uint32_t mss);
  void Forward(Message message) override final;
  void Process(TimeStamp received, Slice packet);
  virtual void Emit(Slice packet) = 0;
  LinkMetrics GetLinkMetrics() override final;

 private:
  void BuildAndSendPacket();
  void SendPacket(SeqNum seq, Slice data, StatusCallback done) override final;
  Status ProcessBody(TimeStamp received, Slice packet);

  Router* const router_;
  const NodeId peer_;
  const uint64_t label_;
  uint64_t metrics_version_ = 1;
  PacketProtocol protocol_;
  bool sending_ = false;

  // data for a send
  std::vector<Slice> send_slices_;
  std::vector<StatusCallback> sending_callbacks_;

  std::queue<Message> outgoing_;
};

}  // namespace overnet
