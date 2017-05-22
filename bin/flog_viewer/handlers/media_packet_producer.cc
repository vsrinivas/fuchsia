// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_packet_producer.h"

#include <iostream>

#include "apps/media/services/logs/media_packet_producer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPacketProducer::MediaPacketProducer(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaPacketProducerAccumulator>()) {
  stub_.set_sink(this);
}

MediaPacketProducer::~MediaPacketProducer() {}

void MediaPacketProducer::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPacketProducer::GetAccumulator() {
  return accumulator_;
}

void MediaPacketProducer::ConnectedTo(uint64_t related_koid) {
  terse_out() << entry() << "MediaPacketProducer.ConnectedTo" << std::endl;
  terse_out() << indent;
  terse_out() << begl << "related_koid: " << AsKoid(related_koid) << std::endl;
  terse_out() << outdent;

  if (accumulator_->consumer_) {
    ReportProblem() << "ConnectedTo when already connected";
  }

  SetBindingKoid(&accumulator_->consumer_, related_koid);
}

void MediaPacketProducer::Resetting() {
  terse_out() << entry() << "MediaPacketProducer.Resetting" << std::endl;

  accumulator_->consumer_.Reset();
}

void MediaPacketProducer::RequestingFlush() {
  terse_out() << entry() << "MediaPacketProducer.RequestingFlush" << std::endl;

  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "RequestingFlush when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketProducer::FlushCompleted() {
  terse_out() << entry() << "MediaPacketProducer.FlushCompleted" << std::endl;

  if (accumulator_->flush_requests_.outstanding_count() == 0) {
    ReportProblem() << "FlushCompleted when no flush was outstanding";
  } else {
    accumulator_->flush_requests_.Remove();
  }
}

void MediaPacketProducer::AllocatingPayloadBuffer(uint32_t index,
                                                  uint64_t size,
                                                  uint64_t buffer) {
  full_out() << entry() << "MediaPacketProducer.AllocatingPayloadBuffer"
             << std::endl;
  full_out() << indent;
  full_out() << begl << "index: " << index << std::endl;
  full_out() << begl << "size: " << size << std::endl;
  full_out() << begl << "buffer: " << AsAddress(buffer) << std::endl;
  full_out() << outdent;

  auto iter = accumulator_->outstanding_allocations_.find(buffer);
  if (iter != accumulator_->outstanding_allocations_.end()) {
    ReportProblem() << "Allocation of buffer already allocated";
  }

  accumulator_->outstanding_allocations_.emplace(
      buffer, MediaPacketProducerAccumulator::Allocation(index, size, buffer));
  accumulator_->allocations_.Add(size);
}

void MediaPacketProducer::PayloadBufferAllocationFailure(uint32_t index,
                                                         uint64_t size) {
  terse_out() << entry() << "MediaPacketProducer.PayloadBufferAllocationFailure"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "index: " << index << std::endl;
  terse_out() << begl << "size: " << size << std::endl;
  terse_out() << outdent;

  ReportProblem() << "Allocation failure";
}

void MediaPacketProducer::ReleasingPayloadBuffer(uint32_t index,
                                                 uint64_t buffer) {
  full_out() << entry() << "MediaPacketProducer.ReleasingPayloadBuffer"
             << std::endl;
  full_out() << indent;
  full_out() << begl << "index: " << index << std::endl;
  full_out() << begl << "buffer: " << AsAddress(buffer) << std::endl;
  full_out() << outdent;

  auto iter = accumulator_->outstanding_allocations_.find(buffer);
  if (iter == accumulator_->outstanding_allocations_.end()) {
    ReportProblem() << "Release of buffer not currently allocated";
    return;
  }

  accumulator_->allocations_.Remove(iter->second.size_);
  accumulator_->outstanding_allocations_.erase(iter);
}

void MediaPacketProducer::DemandUpdated(media::MediaPacketDemandPtr demand) {
  full_out() << entry() << indent;
  full_out() << "MediaPacketProducer.DemandUpdated" << std::endl;
  full_out() << begl << "demand: " << demand << std::endl;
  full_out() << outdent;

  accumulator_->current_demand_ = std::move(demand);
  if (accumulator_->min_packets_outstanding_highest_ <
      accumulator_->current_demand_->min_packets_outstanding) {
    accumulator_->min_packets_outstanding_highest_ =
        accumulator_->current_demand_->min_packets_outstanding;
  }
}

void MediaPacketProducer::ProducingPacket(uint64_t label,
                                          media::MediaPacketPtr packet,
                                          uint64_t payload_address,
                                          uint32_t packets_outstanding) {
  full_out() << entry() << "MediaPacketProducer.ProducingPacket" << std::endl;
  full_out() << indent;
  full_out() << begl << "label: " << label << std::endl;
  full_out() << begl << "packet: " << packet << std::endl;
  full_out() << begl << "payload_address: " << AsAddress(payload_address)
             << std::endl;
  full_out() << begl << "packets_outstanding: " << packets_outstanding
             << std::endl;
  full_out() << outdent;

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

void MediaPacketProducer::RetiringPacket(uint64_t label,
                                         uint32_t packets_outstanding) {
  full_out() << entry() << "MediaPacketProducer.RetiringPacket" << std::endl;
  full_out() << indent;
  full_out() << begl << "label: " << label << std::endl;
  full_out() << begl << "packets_outstanding: " << packets_outstanding
             << std::endl;
  full_out() << outdent;

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
  os << begl << "consumer: " << consumer_ << std::endl;
  os << begl << "flushes: " << flush_requests_.count() << std::endl;

  os << begl << "current demand: " << current_demand_ << std::endl;
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

  os << begl << "allocation count: " << allocations_.count();
  if (allocations_.count() != 0) {
    os << std::endl
       << begl << "allocation size: "
       << "min " << allocations_.min() << ", avg " << allocations_.average()
       << ", max " << allocations_.max() << ", total " << allocations_.total();
  }

  for (const std::pair<uint64_t, std::shared_ptr<Packet>>& pair :
       outstanding_packets_) {
    os << std::endl << begl << "SUSPENSE: outstanding packet" << std::endl;
    os << indent;
    os << begl << "label: " << pair.second->label_ << std::endl;
    os << begl << "packet: " << pair.second->packet_ << std::endl;
    os << begl
       << "payload address: " << AsAddress(pair.second->payload_address_)
       << std::endl;
    os << begl << "packets outstanding: " << pair.second->packets_outstanding_;
    os << outdent;
  }

  for (const std::pair<uint64_t, Allocation>& pair : outstanding_allocations_) {
    os << std::endl << begl << "SUSPENSE: outstanding allocation" << std::endl;
    os << indent;
    os << begl << "index: " << pair.second.index_ << std::endl;
    os << begl << "size: " << pair.second.size_ << std::endl;
    os << begl << "buffer: " << AsAddress(pair.second.buffer_);
    os << outdent;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
