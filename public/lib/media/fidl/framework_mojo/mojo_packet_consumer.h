// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_PACKET_CONSUMER_H_
#define SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_PACKET_CONSUMER_H_

#include "mojo/services/media/common/cpp/media_packet_consumer_base.h"
#include "mojo/services/media/common/interfaces/media_transport.mojom.h"
#include "services/media/framework/models/active_source.h"

namespace mojo {
namespace media {

// Implements MediaPacketConsumer to receive a stream from across mojo.
class MojoPacketConsumer : public MediaPacketConsumerBase, public ActiveSource {
 public:
  using FlushRequestedCallback = std::function<void(const FlushCallback&)>;

  static std::shared_ptr<MojoPacketConsumer> Create() {
    return std::shared_ptr<MojoPacketConsumer>(new MojoPacketConsumer());
  }

  ~MojoPacketConsumer() override;

  // Binds.
  void Bind(InterfaceRequest<MediaPacketConsumer> packet_consumer_request);

  // Sets a callback signalling that a flush has been requested from the
  // MediaPacketConsumer client.
  void SetFlushRequestedCallback(const FlushRequestedCallback& callback);

 private:
  MojoPacketConsumer();

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
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_MOJO_MOJO_PACKET_CONSUMER_H_
