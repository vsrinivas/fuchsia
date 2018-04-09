// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_FIDL_FIDL_PACKET_PRODUCER_H_
#define GARNET_BIN_MEDIA_FIDL_FIDL_PACKET_PRODUCER_H_

#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media_player.h>
#include "garnet/bin/media/framework/models/active_sink.h"
#include "garnet/bin/media/framework/payload_allocator.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/media/transport/media_packet_producer_base.h"

namespace media_player {

// Implements MediaPacketProducer to forward a stream across fidl.
class FidlPacketProducer
    : public media::MediaPacketProducerBase,
      public media::MediaPacketProducer,
      public ActiveSink,
      public media::PayloadAllocator,
      public std::enable_shared_from_this<FidlPacketProducer> {
 public:
  using ConnectionStateChangedCallback = std::function<void()>;
  using FlushConnectionCallback = std::function<void()>;

  static std::shared_ptr<FidlPacketProducer> Create();

  FidlPacketProducer();

  ~FidlPacketProducer() override;

  // Binds.
  void Bind(fidl::InterfaceRequest<media::MediaPacketProducer> request);

  // Sets a callback called whenever the connection state changes.
  void SetConnectionStateChangedCallback(
      ConnectionStateChangedCallback callback);

  // Flushes and tells the connected consumer to flush.
  void FlushConnection(bool hold_frame, FlushConnectionCallback callback);

  // ActiveSink implementation.
  std::shared_ptr<media::PayloadAllocator> allocator() override;

  Demand SupplyPacket(media::PacketPtr packet) override;

  // MediaPacketProducer implementation.
  void Connect(fidl::InterfaceHandle<media::MediaPacketConsumer> consumer,
               ConnectCallback callback) override;

  void Disconnect() override;

  // PayloadAllocator implementation:
  void* AllocatePayloadBuffer(size_t size) override;

  // Releases a buffer previously allocated via AllocatePayloadBuffer.
  void ReleasePayloadBuffer(void* buffer) override;

 protected:
  // MediaPacketProducerBase overrides.
  void OnDemandUpdated(uint32_t min_packets_outstanding,
                       int64_t min_pts) override;

  void OnFailure() override;

 private:
  // Sends a packet to the consumer.
  void SendPacket(media::PacketPtr packet);

  // Shuts down the producer.
  void Reset();

  // Determines the current demand. The |additional_packets_outstanding|
  // parameter indicates the number of packets that should be added to the
  // current outstanding packet count when determining demand. For example, a
  // value of 1 means that the function should determine demand as if one
  // additional packet was outstanding.
  Demand CurrentDemand(uint32_t additional_packets_outstanding = 0);

  fidl::Binding<media::MediaPacketProducer> binding_;

  async_t* async_;
  ConnectionStateChangedCallback connectionStateChangedCallback_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_FIDL_FIDL_PACKET_PRODUCER_H_
