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
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketProducer.ConnectedTo\n";
  terse_out() << indent;
  terse_out() << begl << "related_koid: " << AsKoid(related_koid) << "\n";
  terse_out() << outdent;

  if (accumulator_->consumer_) {
    ReportProblem() << "ConnectedTo when already connected";
  }

  SetBindingKoid(&accumulator_->consumer_, related_koid);
}

void MediaPacketProducer::Resetting() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketProducer.Resetting\n";

  accumulator_->consumer_.Reset();
}

void MediaPacketProducer::RequestingFlush() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketProducer.RequestingFlush\n";

  if (accumulator_->flush_requests_.outstanding_count() != 0) {
    ReportProblem() << "RequestingFlush when another flush was outstanding";
  }
  accumulator_->flush_requests_.Add();
}

void MediaPacketProducer::FlushCompleted() {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketProducer.FlushCompleted\n";

  if (accumulator_->flush_requests_.outstanding_count() == 0) {
    ReportProblem() << "FlushCompleted when no flush was outstanding";
  } else {
    accumulator_->flush_requests_.Remove();
  }
}

void MediaPacketProducer::PayloadBufferAllocationFailure(uint32_t index,
                                                         uint64_t size) {
  terse_out() << AsEntryIndex(entry_index()) << " " << entry()
              << "MediaPacketProducer.PayloadBufferAllocationFailure"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "index: " << index << "\n";
  terse_out() << begl << "size: " << size << "\n";
  terse_out() << outdent;

  ReportProblem() << "Allocation failure";
}

void MediaPacketProducer::DemandUpdated(media::MediaPacketDemandPtr demand) {
  full_out() << AsEntryIndex(entry_index()) << " " << entry() << indent;
  full_out() << "MediaPacketProducer.DemandUpdated\n";
  full_out() << begl << "demand: " << demand << "\n";
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
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketProducer.ProducingPacket\n";
  full_out() << indent;
  full_out() << begl << "label: " << label << "\n";
  full_out() << begl << "packet: " << packet << "\n";
  full_out() << begl << "payload_address: " << AsAddress(payload_address)
             << "\n";
  full_out() << begl << "packets_outstanding: " << packets_outstanding << "\n";
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
  full_out() << AsEntryIndex(entry_index()) << " " << entry()
             << "MediaPacketProducer.RetiringPacket\n";
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

MediaPacketProducerAccumulator::MediaPacketProducerAccumulator() {}

MediaPacketProducerAccumulator::~MediaPacketProducerAccumulator() {}

void MediaPacketProducerAccumulator::Print(std::ostream& os) {
  os << "MediaPacketProducer\n";
  os << indent;
  os << begl << "consumer: " << consumer_ << "\n";
  os << begl << "flushes: " << flush_requests_.count() << "\n";

  os << begl << "current demand: " << current_demand_ << "\n";
  os << begl << "min packets outstanding : max "
     << min_packets_outstanding_highest_ << "\n";

  os << begl << "outstanding packet count: "
     << "curr " << packets_.outstanding_count() << ", max "
     << packets_.max_outstanding_count() << "\n";
  if (packets_.count() != 0) {
    os << begl << "outstanding packet size: "
       << "curr " << packets_.outstanding_total() << ", max "
       << packets_.max_outstanding_total() << "\n";
  }

  os << begl << "packet count: " << packets_.count();
  if (packets_.count() != 0) {
    os << "\n"
       << begl << "packet size: "
       << "min " << packets_.min() << ", avg " << packets_.average() << ", max "
       << packets_.max() << ", total " << packets_.total();
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
