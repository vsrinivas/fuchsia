// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_PRODUCER_DIGEST_H_
#define EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_PRODUCER_DIGEST_H_

#include <unordered_map>

#include "examples/flog_viewer/accumulator.h"
#include "examples/flog_viewer/channel_handler.h"
#include "examples/flog_viewer/counted.h"
#include "examples/flog_viewer/tracked.h"
#include "mojo/services/media/logs/interfaces/media_packet_producer_channel.mojom.h"

namespace mojo {
namespace flog {
namespace examples {
namespace handlers {
namespace media {

class MediaPacketProducerAccumulator;

// Handler for MediaPacketProducerChannel messages, digest format.
class MediaPacketProducerDigest
    : public ChannelHandler,
      public mojo::media::logs::MediaPacketProducerChannel {
 public:
  MediaPacketProducerDigest(const std::string& format);

  ~MediaPacketProducerDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(Message* message) override;

 private:
  // MediaPacketProducerChannel implementation.
  void Connecting() override;

  void Resetting() override;

  void RequestingFlush() override;

  void FlushCompleted() override;

  void AllocatingPayloadBuffer(uint32_t index,
                               uint64_t size,
                               uint64_t buffer) override;

  void PayloadBufferAllocationFailure(uint32_t index, uint64_t size) override;

  void ReleasingPayloadBuffer(uint32_t index, uint64_t buffer) override;

  void DemandUpdated(mojo::media::MediaPacketDemandPtr demand) override;

  void ProducingPacket(uint64_t label,
                       mojo::media::MediaPacketPtr packet,
                       uint64_t payload_address,
                       uint32_t packets_outstanding) override;

  void RetiringPacket(uint64_t label, uint32_t packets_outstanding) override;

 private:
  mojo::media::logs::MediaPacketProducerChannelStub stub_;
  std::shared_ptr<MediaPacketProducerAccumulator> accumulator_;
};

// Status of a media packet producer as understood by MediaPacketProducerDigest.
class MediaPacketProducerAccumulator : public Accumulator {
 public:
  MediaPacketProducerAccumulator();
  ~MediaPacketProducerAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  struct Packet {
    Packet(uint64_t label,
           mojo::media::MediaPacketPtr packet,
           uint64_t payload_address,
           uint32_t packets_outstanding)
        : label_(label),
          packet_(*packet),
          payload_address_(payload_address),
          packets_outstanding_(packets_outstanding) {}
    uint64_t label_;
    mojo::media::MediaPacket packet_;
    uint64_t payload_address_;
    uint32_t packets_outstanding_;
  };

  struct Allocation {
    Allocation(uint32_t index, uint64_t size, uint64_t buffer)
        : index_(index), size_(size), buffer_(buffer) {}
    uint32_t index_;
    uint64_t size_;
    uint64_t buffer_;
  };

  bool connected_ = false;
  Counted flush_requests_;
  mojo::media::MediaPacketDemandPtr current_demand_;
  uint32_t min_packets_outstanding_highest_ = 0;

  std::unordered_map<uint64_t, Packet> outstanding_packets_;
  Tracked packets_;

  std::unordered_map<uint64_t, Allocation> outstanding_allocations_;
  Tracked allocations_;

  friend class MediaPacketProducerDigest;
};

}  // namespace media
}  // namespace handlers
}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_HANDLERS_MEDIA_MEDIA_PACKET_PRODUCER_DIGEST_H_
