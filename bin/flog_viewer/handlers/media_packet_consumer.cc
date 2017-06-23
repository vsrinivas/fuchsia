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
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaPacketConsumer::DemandSet(media::MediaPacketDemandPtr demand) {
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketConsumer.DemandSet\n";
  full_out() << indent;
  full_out() << begl << "demand: " << demand << "\n";
  full_out() << outdent;

  accumulator_->current_demand_ = std::move(demand);
  if (accumulator_->min_packets_outstanding_highest_ <
      accumulator_->current_demand_->min_packets_outstanding) {
    accumulator_->min_packets_outstanding_highest_ =
        accumulator_->current_demand_->min_packets_outstanding;
  }
}

void MediaPacketConsumer::Reset() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.Reset\n";
}

void MediaPacketConsumer::Failed() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.Failed\n";

  accumulator_->failed_ = true;
}

void MediaPacketConsumer::RespondingToGetDemandUpdate(
    media::MediaPacketDemandPtr demand) {
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketConsumer.RespondingToGetDemandUpdate"
             << "\n";
  full_out() << indent;
  full_out() << begl << "demand: " << demand << "\n";
  full_out() << outdent;

  accumulator_->get_demand_update_responses_ += 1;
}

void MediaPacketConsumer::AddPayloadBufferRequested(uint32_t id,
                                                    uint64_t size) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.AddPayloadBufferRequested"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "id: " << id << "\n";
  terse_out() << begl << "size: " << size << "\n";
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
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.RemovePayloadBufferRequested"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "id: " << id << "\n";
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
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.FlushRequested\n";

  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "FlushRequested when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketConsumer::CompletingFlush() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketConsumer.CompletingFlush\n";

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
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketConsumer.PacketSupplied\n";
  full_out() << indent;
  full_out() << begl << "label: " << label << "\n";
  full_out() << begl << "packet: " << packet << "\n";
  full_out() << begl << "payload_address: " << AsAddress(payload_address)
             << "\n";
  full_out() << begl << "packets_outstanding: " << packets_outstanding << "\n";
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
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketConsumer.ReturningPacket\n";
  full_out() << indent;
  full_out() << begl << "label: " << label << "\n";
  full_out() << begl << "packets_outstanding: " << packets_outstanding << "\n";
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
  os << "MediaPacketConsumer\n";
  os << indent;
  os << begl << "GetDemandUpdate responses: " << get_demand_update_responses_
     << "\n";
  os << begl << "flushes: " << flush_requests_.count() << "\n";

  os << begl << "current demand: " << current_demand_ << "\n";
  os << begl << "min packets outstanding: max "
     << min_packets_outstanding_highest_ << "\n";

  os << begl << "outstanding packet count: curr "
     << packets_.outstanding_count() << ", max "
     << packets_.max_outstanding_count() << "\n";
  if (packets_.count() != 0) {
    os << begl << "outstanding packet size: curr "
       << packets_.outstanding_total() << ", max "
       << packets_.max_outstanding_total() << "\n";
  }

  os << begl << "packet count: " << packets_.count() << "\n";
  if (packets_.count() != 0) {
    os << begl << "packet size: "
       << "min " << packets_.min() << ", avg " << packets_.average() << ", max "
       << packets_.max() << ", total " << packets_.total() << "\n";
  }

  os << begl << "outstanding payload buffer count: "
     << "curr " << buffers_.outstanding_count() << ", max "
     << buffers_.max_outstanding_count() << "\n";
  if (buffers_.count() != 0) {
    os << begl << "outstanding payload buffer size: "
       << "curr " << buffers_.outstanding_total() << ", max "
       << buffers_.max_outstanding_total() << "\n";
  }

  os << begl << "payload buffer count: " << buffers_.count();
  if (buffers_.count() != 0) {
    os << "\n"
       << begl << "payload buffer size: "
       << "min " << buffers_.min() << ", avg " << buffers_.average() << ", max "
       << buffers_.max() << ", total " << buffers_.total();
  }

  for (const std::pair<uint64_t, std::shared_ptr<Packet>>& pair :
       outstanding_packets_) {
    os << "\n" << begl << "SUSPENSE: outstanding packet\n";
    os << indent;
    os << begl << "label: " << pair.second->label_ << "\n";
    os << begl << "packet: " << pair.second->packet_ << "\n";
    os << begl
       << "payload address: " << AsAddress(pair.second->payload_address_)
       << "\n";
    os << begl << "packets outstanding: " << pair.second->packets_outstanding_;
    os << outdent;
  }

  for (const std::pair<uint32_t, PayloadBuffer>& pair :
       outstanding_payload_buffers_) {
    os << "\n" << begl << "SUSPENSE: outstanding payload buffer\n";
    os << indent;
    os << begl << "id: " << pair.second.id_ << "\n";
    os << begl << "size: " << pair.second.size_;
    os << outdent;
  }

  if (current_demand_ &&
      current_demand_->min_packets_outstanding > packets_.outstanding_count()) {
    os << "\n" << begl << "SUSPENSE: unmet packet demand\n";
    os << indent;
    os << begl << "demand: " << current_demand_->min_packets_outstanding
       << "\n";
    os << begl << "supply: " << packets_.outstanding_count();
    os << outdent;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
