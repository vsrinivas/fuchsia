// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_packet_producer_digest.h"

#include <iostream>

#include "apps/media/services/logs/media_packet_producer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPacketProducerDigest::MediaPacketProducerDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaPacketProducerAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaPacketProducerDigest::~MediaPacketProducerDigest() {}

void MediaPacketProducerDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPacketProducerDigest::GetAccumulator() {
  return accumulator_;
}

void MediaPacketProducerDigest::ConnectedTo(uint64_t related_koid) {
  if (accumulator_->consumer_) {
    ReportProblem() << "ConnectedTo when already connected";
  }

  SetBindingKoid(&accumulator_->consumer_, related_koid);
}

void MediaPacketProducerDigest::Resetting() {
  accumulator_->consumer_.Reset();
}

void MediaPacketProducerDigest::RequestingFlush() {
  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "RequestingFlush when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketProducerDigest::FlushCompleted() {
  if (accumulator_->flush_requests_.outstanding_count() == 0) {
    ReportProblem() << "FlushCompleted when no flush was outstanding";
  } else {
    accumulator_->flush_requests_.Remove();
  }
}

void MediaPacketProducerDigest::AllocatingPayloadBuffer(uint32_t index,
                                                        uint64_t size,
                                                        uint64_t buffer) {
  auto iter = accumulator_->outstanding_allocations_.find(buffer);
  if (iter != accumulator_->outstanding_allocations_.end()) {
    ReportProblem() << "Allocation of buffer already allocated";
  }

  accumulator_->outstanding_allocations_.emplace(
      buffer, MediaPacketProducerAccumulator::Allocation(index, size, buffer));
  accumulator_->allocations_.Add(size);
}

void MediaPacketProducerDigest::PayloadBufferAllocationFailure(uint32_t index,
                                                               uint64_t size) {
  ReportProblem() << "Allocation failure";
}

void MediaPacketProducerDigest::ReleasingPayloadBuffer(uint32_t index,
                                                       uint64_t buffer) {
  auto iter = accumulator_->outstanding_allocations_.find(buffer);
  if (iter == accumulator_->outstanding_allocations_.end()) {
    ReportProblem() << "Release of buffer not currently allocated";
    return;
  }

  accumulator_->allocations_.Remove(iter->second.size_);
  accumulator_->outstanding_allocations_.erase(iter);
}

void MediaPacketProducerDigest::DemandUpdated(
    media::MediaPacketDemandPtr demand) {
  accumulator_->current_demand_ = std::move(demand);
  if (accumulator_->min_packets_outstanding_highest_ <
      accumulator_->current_demand_->min_packets_outstanding) {
    accumulator_->min_packets_outstanding_highest_ =
        accumulator_->current_demand_->min_packets_outstanding;
  }
}

void MediaPacketProducerDigest::ProducingPacket(uint64_t label,
                                                media::MediaPacketPtr packet,
                                                uint64_t payload_address,
                                                uint32_t packets_outstanding) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter != accumulator_->outstanding_packets_.end()) {
    ReportProblem() << "Packet label " << label << " reused";
  }

  accumulator_->packets_.Add(packet->payload_size);

  accumulator_->outstanding_packets_.emplace(
      label,
      MediaPacketProducerAccumulator::Packet::Create(
          label, std::move(packet), payload_address, packets_outstanding));
}

void MediaPacketProducerDigest::RetiringPacket(uint64_t label,
                                               uint32_t packets_outstanding) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter == accumulator_->outstanding_packets_.end()) {
    ReportProblem() << "Retiring packet not currently outstanding";
    return;
  }

  accumulator_->packets_.Remove(iter->second->packet_->payload_size);
  accumulator_->outstanding_packets_.erase(iter);
}

MediaPacketProducerAccumulator::MediaPacketProducerAccumulator() {}

MediaPacketProducerAccumulator::~MediaPacketProducerAccumulator() {}

void MediaPacketProducerAccumulator::Print(std::ostream& os) {
  os << "MediaPacketProducer" << std::endl;
  os << indent;
  os << begl << "consumer: " << consumer_;
  os << begl << "flushes: " << flush_requests_.count() << std::endl;

  os << begl << "current demand: " << current_demand_;
  os << begl << "min packets outstanding : max "
     << min_packets_outstanding_highest_ << std::endl;

  os << begl << "outstanding packet count: "
     << "curr " << packets_.outstanding_count() << ", max "
     << packets_.max_outstanding_count() << std::endl;
  if (packets_.count() != 0) {
    os << begl << "outstanding packet size: "
       << "curr " << packets_.outstanding_total() << ", max "
       << packets_.max_outstanding_total() << std::endl;
  }

  os << begl << "packet count: " << packets_.count() << std::endl;
  if (packets_.count() != 0) {
    os << begl << "packet size: "
       << "min " << packets_.min() << ", avg " << packets_.average() << ", max "
       << packets_.max() << ", total " << packets_.total() << std::endl;
  }

  os << begl << "outstanding allocation count: "
     << "curr " << allocations_.outstanding_count() << ", max "
     << allocations_.max_outstanding_count() << std::endl;
  if (allocations_.count() != 0) {
    os << begl << "outstanding allocation size: "
       << "curr " << allocations_.outstanding_total() << ", max "
       << allocations_.max_outstanding_total() << std::endl;
  }

  os << begl << "allocation count: " << allocations_.count() << std::endl;
  if (allocations_.count() != 0) {
    os << begl << "allocation size: "
       << "min " << allocations_.min() << ", avg " << allocations_.average()
       << ", max " << allocations_.max() << ", total " << allocations_.total()
       << std::endl;
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

  for (const std::pair<uint64_t, Allocation>& pair : outstanding_allocations_) {
    os << begl << "SUSPENSE: outstanding allocation" << std::endl;
    os << indent;
    os << begl << "index: " << pair.second.index_ << std::endl;
    os << begl << "size: " << pair.second.size_ << std::endl;
    os << begl << "buffer: " << AsAddress(pair.second.buffer_) << std::endl;
    os << outdent;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
