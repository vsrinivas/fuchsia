// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPacketConsumerDigest::MediaPacketConsumerDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaPacketConsumerAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaPacketConsumerDigest::~MediaPacketConsumerDigest() {}

void MediaPacketConsumerDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPacketConsumerDigest::GetAccumulator() {
  return accumulator_;
}

std::shared_ptr<MediaPacketConsumerAccumulator::Packet>
MediaPacketConsumerDigest::FindOutstandingPacket(uint64_t label) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter == accumulator_->outstanding_packets_.end()) {
    return nullptr;
  }

  return iter->second;
}

void MediaPacketConsumerDigest::BoundAs(uint64_t koid) {
  BindAs(koid);
}

void MediaPacketConsumerDigest::DemandSet(media::MediaPacketDemandPtr demand) {
  accumulator_->current_demand_ = std::move(demand);
  if (accumulator_->min_packets_outstanding_highest_ <
      accumulator_->current_demand_->min_packets_outstanding) {
    accumulator_->min_packets_outstanding_highest_ =
        accumulator_->current_demand_->min_packets_outstanding;
  }
}

void MediaPacketConsumerDigest::Reset() {}

void MediaPacketConsumerDigest::Failed() {
  accumulator_->failed_ = true;
}

void MediaPacketConsumerDigest::RespondingToGetDemandUpdate(
    media::MediaPacketDemandPtr demand) {
  accumulator_->get_demand_update_responses_ += 1;
}

void MediaPacketConsumerDigest::AddPayloadBufferRequested(uint32_t id,
                                                          uint64_t size) {
  auto iter = accumulator_->outstanding_payload_buffers_.find(id);
  if (iter != accumulator_->outstanding_payload_buffers_.end()) {
    ReportProblem() << "Payload buffer added with id already in use";
  }

  accumulator_->outstanding_payload_buffers_.emplace(
      id, MediaPacketConsumerAccumulator::PayloadBuffer(id, size));
  accumulator_->buffers_.Add(size);
}

void MediaPacketConsumerDigest::RemovePayloadBufferRequested(uint32_t id) {
  auto iter = accumulator_->outstanding_payload_buffers_.find(id);
  if (iter == accumulator_->outstanding_payload_buffers_.end()) {
    ReportProblem() << "RemovePayloadBuffer request specifies unassigned id";
    return;
  }

  accumulator_->buffers_.Remove(iter->second.size_);
  accumulator_->outstanding_payload_buffers_.erase(iter);
}

void MediaPacketConsumerDigest::FlushRequested() {
  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "FlushRequested when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketConsumerDigest::CompletingFlush() {
  if (accumulator_->flush_requests_.outstanding_count() == 0) {
    ReportProblem() << "CompletingFlush when no flush was outstanding";
  } else {
    accumulator_->flush_requests_.Remove();
  }
}

void MediaPacketConsumerDigest::PacketSupplied(uint64_t label,
                                               media::MediaPacketPtr packet,
                                               uint64_t payload_address,
                                               uint32_t packets_outstanding) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter != accumulator_->outstanding_packets_.end()) {
    ReportProblem() << "Packet label reused";
  }

  accumulator_->packets_.Add(packet->payload_size);

  accumulator_->outstanding_packets_.emplace(
      label, MediaPacketConsumerAccumulator::Packet::Create(
                 label, std::move(packet), payload_address, packets_outstanding,
                 entry()->time_ns));
}

void MediaPacketConsumerDigest::ReturningPacket(uint64_t label,
                                                uint32_t packets_outstanding) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter == accumulator_->outstanding_packets_.end()) {
    ReportProblem() << "Retiring packet not currently outstanding";
    return;
  }

  accumulator_->packets_.Remove(iter->second->packet_->payload_size);
  accumulator_->outstanding_packets_.erase(iter);
}

MediaPacketConsumerAccumulator::MediaPacketConsumerAccumulator() {}

MediaPacketConsumerAccumulator::~MediaPacketConsumerAccumulator() {}

void MediaPacketConsumerAccumulator::Print(std::ostream& os) {
  os << "MediaPacketConsumer" << std::endl;
  os << indent;
  os << begl << "GetDemandUpdate responses: " << get_demand_update_responses_
     << std::endl;
  os << begl << "flushes: " << flush_requests_.count() << std::endl;

  os << begl << "current demand: " << current_demand_;
  os << begl << "min packets outstanding: max "
     << min_packets_outstanding_highest_ << std::endl;

  os << begl << "outstanding packet count: curr "
     << packets_.outstanding_count() << ", max "
     << packets_.max_outstanding_count() << std::endl;
  if (packets_.count() != 0) {
    os << begl << "outstanding packet size: curr "
       << packets_.outstanding_total() << ", max "
       << packets_.max_outstanding_total() << std::endl;
  }

  os << begl << "packet count: " << packets_.count() << std::endl;
  if (packets_.count() != 0) {
    os << begl << "packet size: "
       << "min " << packets_.min() << ", avg " << packets_.average() << ", max "
       << packets_.max() << ", total " << packets_.total() << std::endl;
  }

  os << begl << "outstanding payload buffer count: "
     << "curr " << buffers_.outstanding_count() << ", max "
     << buffers_.max_outstanding_count() << std::endl;
  if (buffers_.count() != 0) {
    os << begl << "outstanding payload buffer size: "
       << "curr " << buffers_.outstanding_total() << ", max "
       << buffers_.max_outstanding_total() << std::endl;
  }

  os << begl << "payload buffer count: " << buffers_.count() << std::endl;
  if (buffers_.count() != 0) {
    os << begl << "payload buffer size: "
       << "min " << buffers_.min() << ", avg " << buffers_.average() << ", max "
       << buffers_.max() << ", total " << buffers_.total() << std::endl;
  }

  for (const std::pair<uint64_t, std::shared_ptr<Packet>>& pair :
       outstanding_packets_) {
    os << begl << "SUSPENSE: outstanding packet" << std::endl;
    os << indent;
    os << begl << "label: " << pair.second->label_ << std::endl;
    os << begl << "packet: " << pair.second->packet_;
    os << begl
       << "payload address: " << AsAddress(pair.second->payload_address_)
       << std::endl;
    os << begl << "packets outstanding: " << pair.second->packets_outstanding_
       << std::endl;
    os << outdent;
  }

  for (const std::pair<uint32_t, PayloadBuffer>& pair :
       outstanding_payload_buffers_) {
    os << begl << "SUSPENSE: outstanding payload buffer" << std::endl;
    os << indent;
    os << begl << "id: " << pair.second.id_ << std::endl;
    os << begl << "size: " << pair.second.size_ << std::endl;
    os << outdent;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
