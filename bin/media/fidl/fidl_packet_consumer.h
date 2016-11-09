// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/cpp/media_packet_consumer_base.h"
#include "apps/media/interfaces/media_transport.fidl.h"
#include "apps/media/src/framework/models/active_source.h"

namespace media {

// Implements MediaPacketConsumer to receive a stream from across fidl.
class FidlPacketConsumer : public MediaPacketConsumerBase, public ActiveSource {
 public:
  using FlushRequestedCallback = std::function<void(const FlushCallback&)>;

  static std::shared_ptr<FidlPacketConsumer> Create() {
    return std::shared_ptr<FidlPacketConsumer>(new FidlPacketConsumer());
  }

  ~FidlPacketConsumer() override;

  // Binds.
  void Bind(
      fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request);

  // Sets a callback signalling that a flush has been requested from the
  // MediaPacketConsumer client.
  void SetFlushRequestedCallback(const FlushRequestedCallback& callback);

 private:
  FidlPacketConsumer();

  // MediaPacketConsumerBase overrides.
  void OnPacketSupplied(
      std::unique_ptr<SuppliedPacket> supplied_packet) override;

  void OnPacketReturning() override;

  void OnFlushRequested(const FlushCallback& callback) override;

  // ActiveSource implementation.
  bool can_accept_allocator() const override;

  void set_allocator(PayloadAllocator* allocator) override;

  void SetSupplyCallback(const SupplyCallback& supply_callback) override;

  void SetDownstreamDemand(Demand demand) override;

  // Specialized packet implementation.
  class PacketImpl : public Packet {
   public:
    static PacketPtr Create(std::unique_ptr<SuppliedPacket> supplied_packet) {
      return PacketPtr(new PacketImpl(std::move(supplied_packet)));
    }

   protected:
    void Release() override;

   private:
    PacketImpl(std::unique_ptr<SuppliedPacket> supplied_packet);

    ~PacketImpl() override;

    std::unique_ptr<SuppliedPacket> supplied_packet_;
  };

  Demand downstream_demand_ = Demand::kNegative;
  FlushRequestedCallback flush_requested_callback_;
  SupplyCallback supply_callback_;
};

}  // namespace media
