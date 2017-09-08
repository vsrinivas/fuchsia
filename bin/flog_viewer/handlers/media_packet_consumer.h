// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <unordered_map>

#include "lib/media/fidl/logs/media_packet_consumer_channel.fidl.h"
#include "garent/bin/flog_viewer/accumulator.h"
#include "garent/bin/flog_viewer/channel_handler.h"
#include "garent/bin/flog_viewer/counted.h"
#include "garent/bin/flog_viewer/tracked.h"

namespace flog {
namespace handlers {

// Status of a media packet consumer as understood by MediaPacketConsumer.
class MediaPacketConsumerAccumulator : public Accumulator {
 public:
  MediaPacketConsumerAccumulator();
  ~MediaPacketConsumerAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

  struct Packet {
    static std::shared_ptr<Packet> Create(uint64_t label,
                                          media::MediaPacketPtr packet,
                                          uint64_t payload_address,
                                          uint32_t packets_outstanding,
                                          int64_t time_ns) {
      return std::make_shared<Packet>(label, std::move(packet), payload_address,
                                      packets_outstanding, time_ns);
    }

    Packet(uint64_t label,
           media::MediaPacketPtr packet,
           uint64_t payload_address,
           uint32_t packets_outstanding,
           int64_t time_ns)
        : label_(label),
          packet_(std::move(packet)),
          payload_address_(payload_address),
          packets_outstanding_(packets_outstanding),
          time_ns_(time_ns) {}
    uint64_t label_;
    media::MediaPacketPtr packet_;
    uint64_t payload_address_;
    uint32_t packets_outstanding_;
    int64_t time_ns_;
  };

  struct PayloadBuffer {
    PayloadBuffer(uint32_t id, uint64_t size) : id_(id), size_(size) {}
    uint32_t id_;
    uint64_t size_;
  };

 private:
  bool failed_ = false;
  uint64_t get_demand_update_responses_ = 0;
  Counted flush_requests_;
  media::MediaPacketDemandPtr current_demand_;
  uint32_t min_packets_outstanding_highest_ = 0;

  std::map<uint64_t, std::shared_ptr<Packet>> outstanding_packets_;
  Tracked packets_;

  std::unordered_map<uint32_t, PayloadBuffer> outstanding_payload_buffers_;
  Tracked buffers_;

  friend class MediaPacketConsumer;
};

// Handler for MediaPacketConsumerChannel messages.
class MediaPacketConsumer : public ChannelHandler,
                            public media::logs::MediaPacketConsumerChannel {
 public:
  MediaPacketConsumer(const std::string& format);

  ~MediaPacketConsumer() override;

  std::shared_ptr<Accumulator> GetAccumulator() override;

  std::shared_ptr<MediaPacketConsumerAccumulator::Packet> FindOutstandingPacket(
      uint64_t label);

  const std::map<uint64_t,
                 std::shared_ptr<MediaPacketConsumerAccumulator::Packet>>&
  outstanding_packets() {
    return accumulator_->outstanding_packets_;
  }

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

}  // namespace handlers
}  // namespace flog
