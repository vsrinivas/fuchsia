// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/logs/media_packet_producer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"

namespace flog {
namespace handlers {

// Handler for MediaPacketProducerChannel messages.
class MediaPacketProducerFull : public ChannelHandler,
                                public media::logs::MediaPacketProducerChannel {
 public:
  MediaPacketProducerFull(const std::string& format);

  ~MediaPacketProducerFull() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaPacketProducerChannel implementation.
  void ConnectedTo(uint64_t related_koid) override;

  void Connecting() override;

  void Resetting() override;

  void RequestingFlush() override;

  void FlushCompleted() override;

  void AllocatingPayloadBuffer(uint32_t index,
                               uint64_t size,
                               uint64_t buffer) override;

  void PayloadBufferAllocationFailure(uint32_t index, uint64_t size) override;

  void ReleasingPayloadBuffer(uint32_t index, uint64_t buffer) override;

  void DemandUpdated(media::MediaPacketDemandPtr demand) override;

  void ProducingPacket(uint64_t label,
                       media::MediaPacketPtr packet,
                       uint64_t payload_address,
                       uint32_t packets_outstanding) override;

  void RetiringPacket(uint64_t label, uint32_t packets_outstanding) override;

 private:
  media::logs::MediaPacketProducerChannelStub stub_;
  bool terse_;
};

}  // namespace handlers
}  // namespace flog
