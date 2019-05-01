// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/packet_protocol/packet_protocol.h"
#include "src/connectivity/overnet/lib/routing/router.h"

namespace overnet {

class PacketLink : public Link, private PacketProtocol::PacketSender {
 public:
  static inline constexpr auto kModule = Module::PACKET_LINK;

  PacketLink(Router* router, NodeId peer, uint32_t mss, uint64_t label);
  void Close(Callback<void> quiesced) override final;
  void Forward(Message message) override final;
  void Process(TimeStamp received, Slice packet);
  virtual void Emit(Slice packet) = 0;
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override final;
  // Mark this link as inoperable
  virtual void Tombstone();

 private:
  void SchedulePacket();
  void SendPacket(SeqNum seq, LazySlice packet) override final;
  Status ProcessBody(TimeStamp received, Slice packet);
  Slice BuildPacket(LazySliceArgs args);

  Router* const router_;
  Timer* const timer_;
  const NodeId peer_;
  const uint64_t label_;
  uint64_t metrics_version_ = 1;
  PacketProtocol protocol_;
  Optional<MessageWithPayload> stashed_;
  bool sending_ = false;

  class LinkSendRequest final : public PacketProtocol::SendRequest {
   public:
    LinkSendRequest(PacketLink* link);
    ~LinkSendRequest();

    Slice GenerateBytes(LazySliceArgs args) override;
    void Ack(const Status& status) override;

   private:
    const Op op_ = ScopedOp::current();
    PacketLink* const link_;
    bool blocking_sends_ = true;
  };

  // data for a send
  std::vector<Slice> send_slices_;

  std::queue<Message> outgoing_;

  // Pointer to a queue that's being used to queue pending sends.
  // This queue is held on the stack within SendPacket().
  std::queue<LazySlice>* send_packet_queue_ = nullptr;
};

}  // namespace overnet
