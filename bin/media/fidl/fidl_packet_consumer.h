// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/models/active_source.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/media_transport.fidl.h"
#include "lib/media/transport/media_packet_consumer_base.h"

namespace media {

// Implements MediaPacketConsumer to receive a stream from across fidl.
class FidlPacketConsumer : public MediaPacketConsumerBase, public ActiveSource {
 public:
  using FlushRequestedCallback =
      std::function<void(bool hold_frame, const FlushCallback&)>;

  static std::shared_ptr<FidlPacketConsumer> Create() {
    return std::shared_ptr<FidlPacketConsumer>(new FidlPacketConsumer());
  }

  ~FidlPacketConsumer() override;

  // Binds.
  void Bind(fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request,
            const std::function<void()>& connection_error_handler);

  // Sets a callback signalling that a flush has been requested from the
  // MediaPacketConsumer client.
  void SetFlushRequestedCallback(const FlushRequestedCallback& callback);

 private:
  FidlPacketConsumer();

  // MediaPacketConsumerBase overrides.
  void OnPacketSupplied(
      std::unique_ptr<SuppliedPacket> supplied_packet) override;

  void OnPacketReturning() override;

  void OnFlushRequested(bool hold_frame,
                        const FlushCallback& callback) override;

  void OnUnbind() override;

  // ActiveSource implementation.
  bool can_accept_allocator() const override;

  void set_allocator(std::shared_ptr<PayloadAllocator> allocator) override;

  void SetDownstreamDemand(Demand demand) override;

  // Specialized packet implementation.
  class PacketImpl : public Packet {
   public:
    static PacketPtr Create(std::unique_ptr<SuppliedPacket> supplied_packet) {
      return std::make_shared<PacketImpl>(std::move(supplied_packet));
    }

    PacketImpl(std::unique_ptr<SuppliedPacket> supplied_packet);

    uint64_t GetLabel() override;

   private:
    std::unique_ptr<SuppliedPacket> supplied_packet_;
  };

  std::function<void()> unbind_handler_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  Demand downstream_demand_ = Demand::kNegative;
  FlushRequestedCallback flush_requested_callback_;
};

}  // namespace media
