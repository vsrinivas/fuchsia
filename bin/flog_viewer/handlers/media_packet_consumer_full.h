// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaPacketConsumerChannel messages.
class MediaPacketConsumerFull : public ChannelHandler,
                                public media::logs::MediaPacketConsumerChannel {
 public:
  MediaPacketConsumerFull(const std::string& format);

  ~MediaPacketConsumerFull() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaPacketConsumerChannel implementation.
  void DemandSet(media::MediaPacketDemandPtr demand) override;

  void Reset() override;

  void Failed() override;

  void RespondingToGetDemandUpdate(media::MediaPacketDemandPtr demand) override;

  void AddPayloadBufferRequested(uint32_t id, uint64_t size) override;

  void RemovePayloadBufferRequested(uint32_t id) override;

  void FlushRequested() override;

  void CompletingFlush() override;

  void PacketSupplied(uint64_t label,
                      media::MediaPacketPtr packet,
                      uint64_t payload_address,
                      uint32_t packets_outstanding) override;

  void ReturningPacket(uint64_t label, uint32_t packets_outstanding) override;

  media::logs::MediaPacketConsumerChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
