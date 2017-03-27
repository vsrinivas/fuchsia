// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/media/services/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"
#include "apps/media/tools/flog_viewer/counted.h"
#include "apps/media/tools/flog_viewer/tracked.h"

namespace flog {
namespace handlers {

class MediaPacketConsumerAccumulator;

// Handler for MediaPacketConsumerChannel messages, digest format.
class MediaPacketConsumerDigest
    : public ChannelHandler,
      public media::logs::MediaPacketConsumerChannel {
 public:
  MediaPacketConsumerDigest(const std::string& format);

  ~MediaPacketConsumerDigest() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler implementation.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaPacketConsumerChannel implementation.
  void BoundAs(uint64_t koid) override;

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

 private:
  media::logs::MediaPacketConsumerChannelStub stub_;
  std::shared_ptr<MediaPacketConsumerAccumulator> accumulator_;
};

// Status of a media packet consumer as understood by MediaPacketConsumerDigest.
class MediaPacketConsumerAccumulator : public Accumulator {
 public:
  MediaPacketConsumerAccumulator();
  ~MediaPacketConsumerAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  struct Packet {
    static std::shared_ptr<Packet> Create(uint64_t label,
                                          media::MediaPacketPtr packet,
                                          uint64_t payload_address,
                                          uint32_t packets_outstanding) {
      return std::make_shared<Packet>(label, std::move(packet), payload_address,
                                      packets_outstanding);
    }

    Packet(uint64_t label,
           media::MediaPacketPtr packet,
           uint64_t payload_address,
           uint32_t packets_outstanding)
        : label_(label),
          packet_(std::move(packet)),
          payload_address_(payload_address),
          packets_outstanding_(packets_outstanding) {}
    uint64_t label_;
    media::MediaPacketPtr packet_;
    uint64_t payload_address_;
    uint32_t packets_outstanding_;
  };

  struct PayloadBuffer {
    PayloadBuffer(uint32_t id, uint64_t size) : id_(id), size_(size) {}
    uint32_t id_;
    uint64_t size_;
  };

  bool failed_ = false;
  uint64_t get_demand_update_responses_ = 0;
  Counted flush_requests_;
  media::MediaPacketDemandPtr current_demand_;
  uint32_t min_packets_outstanding_highest_ = 0;

  std::unordered_map<uint64_t, std::shared_ptr<Packet>> outstanding_packets_;
  Tracked packets_;

  std::unordered_map<uint32_t, PayloadBuffer> outstanding_payload_buffers_;
  Tracked buffers_;

  friend class MediaPacketConsumerDigest;
};

}  // namespace handlers
}  // namespace flog
