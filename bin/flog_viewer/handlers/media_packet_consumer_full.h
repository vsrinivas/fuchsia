// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_CONSUMER_FULL_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_CONSUMER_FULL_H_

#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/media/logs/interfaces/media_packet_consumer_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

// Handler for MediaPacketConsumerChannel messages.
class MediaPacketConsumerFull
    : public ChannelHandler,
      public mojo::media::logs::MediaPacketConsumerChannel {
 public:
  MediaPacketConsumerFull(const std::string& format);

  ~MediaPacketConsumerFull() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaPacketConsumerChannel implementation.
  void DemandSet(mojo::media::MediaPacketDemandPtr demand) override;

  void Reset() override;

  void Failed() override;

  void RespondingToGetDemandUpdate(
      mojo::media::MediaPacketDemandPtr demand) override;

  void AddPayloadBufferRequested(uint32_t id, uint64_t size) override;

  void RemovePayloadBufferRequested(uint32_t id) override;

  void FlushRequested() override;

  void CompletingFlush() override;

  void PacketSupplied(uint64_t label,
                      mojo::media::MediaPacketPtr packet,
                      uint64_t payload_address,
                      uint32_t packets_outstanding) override;

  void ReturningPacket(uint64_t label, uint32_t packets_outstanding) override;

  mojo::media::logs::MediaPacketConsumerChannelStub stub_;
  bool terse_;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_CONSUMER_FULL_H_
