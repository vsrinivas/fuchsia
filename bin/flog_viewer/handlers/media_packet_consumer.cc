// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer.h"

#include <iostream>

#include "apps/media/services/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPacketConsumer::MediaPacketConsumer(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaPacketConsumerAccumulator>()) {
  stub_.set_sink(this);
}

MediaPacketConsumer::~MediaPacketConsumer() {}

void MediaPacketConsumer::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaPacketConsumer::GetAccumulator() {
  return accumulator_;
}

std::shared_ptr<MediaPacketConsumerAccumulator::Packet>
MediaPacketConsumer::FindOutstandingPacket(uint64_t label) {
  auto iter = accumulator_->outstanding_packets_.find(label);
  if (iter == accumulator_->outstanding_packets_.end()) {
    return nullptr;
  }

  return iter->second;
}

void MediaPacketConsumer::BoundAs(uint64_t koid) {
  terse_out() << entry() << "MediaPacketConsumer.BoundAs" << std::endl;
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << std::endl;
  terse_out() << outdent;

  BindAs(koid);
}

void MediaPacketConsumer::DemandSet(media::MediaPacketDemandPtr demand) {
  full_out() << entry() << "MediaPacketConsumer.DemandSet" << std::endl;
  full_out() << indent;
  full_out() << begl << "demand: " << demand << std::endl;
  full_out() << outdent;

  accumulator_->current_demand_ = std::move(demand);
  if (accumulator_->min_packets_outstanding_highest_ <
      accumulator_->current_demand_->min_packets_outstanding) {
    accumulator_->min_packets_outstanding_highest_ =
        accumulator_->current_demand_->min_packets_outstanding;
  }
}

void MediaPacketConsumer::Reset() {
  terse_out() << entry() << "MediaPacketConsumer.Reset" << std::endl;
}

void MediaPacketConsumer::Failed() {
  terse_out() << entry() << "MediaPacketConsumer.Failed" << std::endl;

  accumulator_->failed_ = true;
}

void MediaPacketConsumer::RespondingToGetDemandUpdate(
    media::MediaPacketDemandPtr demand) {
  full_out() << entry() << "MediaPacketConsumer.RespondingToGetDemandUpdate"
             << std::endl;
  full_out() << indent;
  full_out() << begl << "demand: " << demand << std::endl;
  full_out() << outdent;

  accumulator_->get_demand_update_responses_ += 1;
}

void MediaPacketConsumer::AddPayloadBufferRequested(uint32_t id,
                                                    uint64_t size) {
  terse_out() << entry() << "MediaPacketConsumer.AddPayloadBufferRequested"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "id: " << id << std::endl;
  terse_out() << begl << "size: " << size << std::endl;
  terse_out() << outdent;

  auto iter = accumulator_->outstanding_payload_buffers_.find(id);
  if (iter != accumulator_->outstanding_payload_buffers_.end()) {
    ReportProblem() << "Payload buffer added with id already in use";
  }

  accumulator_->outstanding_payload_buffers_.emplace(
      id, MediaPacketConsumerAccumulator::PayloadBuffer(id, size));
  accumulator_->buffers_.Add(size);
}

void MediaPacketConsumer::RemovePayloadBufferRequested(uint32_t id) {
  terse_out() << entry() << "MediaPacketConsumer.RemovePayloadBufferRequested"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "id: " << id << std::endl;
  terse_out() << outdent;

  auto iter = accumulator_->outstanding_payload_buffers_.find(id);
  if (iter == accumulator_->outstanding_payload_buffers_.end()) {
    ReportProblem() << "RemovePayloadBuffer request specifies unassigned id";
    return;
  }

  accumulator_->buffers_.Remove(iter->second.size_);
  accumulator_->outstanding_payload_buffers_.erase(iter);
}

void MediaPacketConsumer::FlushRequested() {
  terse_out() << entry() << "MediaPacketConsumer.FlushRequested" << std::endl;

  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "FlushRequested when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketConsumer::CompletingFlush() {
  terse_out() << entry() << "MediaPacketConsumer.CompletingFlush" << std::endl;

  if (accumulator_->flush_requests_.outstanding_count() == 0) {
    ReportProblem() << "CompletingFlush when no flush was outstanding";
  } else {
    accumulator_->flush_requests_.Remove();
  }
}

void MediaPacketConsumer::PacketSupplied(uint64_t label,
                                         media::MediaPacketPtr packet,
                                         uint64_t payload_address,
                                         uint32_t packets_outstanding) {
  full_out() << entry() << "MediaPacketConsumer.PacketSupplied" << std::endl;
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
    ReportProblem() << "Packet label reused";
  }

  accumulator_->packets_.Add(packet->payload_size);

  accumulator_->outstanding_packets_.emplace(
      label, MediaPacketConsumerAccumulator::Packet::Create(
                 label, std::move(packet), payload_address, packets_outstanding,
                 entry()->time_ns));
}

void MediaPacketConsumer::ReturningPacket(uint64_t label,
                                          uint32_t packets_outstanding) {
  full_out() << entry() << "MediaPacketConsumer.ReturningPacket" << std::endl;
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

MediaPacketConsumerAccumulator::MediaPacketConsumerAccumulator() {}

MediaPacketConsumerAccumulator::~MediaPacketConsumerAccumulator() {}

void MediaPacketConsumerAccumulator::Print(std::ostream& os) {
  os << "MediaPacketConsumer" << std::endl;
  os << indent;
  os << begl << "GetDemandUpdate responses: " << get_demand_update_responses_
     << std::endl;
  os << begl << "flushes: " << flush_requests_.count() << std::endl;

  os << begl << "current demand: " << current_demand_ << std::endl;
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

  os << begl << "payload buffer count: " << buffers_.count();
  if (buffers_.count() != 0) {
    os << std::endl
       << begl << "payload buffer size: "
       << "min " << buffers_.min() << ", avg " << buffers_.average() << ", max "
       << buffers_.max() << ", total " << buffers_.total();
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

  for (const std::pair<uint32_t, PayloadBuffer>& pair :
       outstanding_payload_buffers_) {
    os << std::endl
       << begl << "SUSPENSE: outstanding payload buffer" << std::endl;
    os << indent;
    os << begl << "id: " << pair.second.id_ << std::endl;
    os << begl << "size: " << pair.second.size_;
    os << outdent;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
