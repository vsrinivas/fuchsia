// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer_full.h"

#include <iostream>

#include "apps/media/interfaces/logs/media_packet_consumer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaPacketConsumerFull::MediaPacketConsumerFull(const std::string& format)
    : terse_(format == FlogViewer::kFormatTerse) {
  stub_.set_sink(this);
}

MediaPacketConsumerFull::~MediaPacketConsumerFull() {}

void MediaPacketConsumerFull::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

void MediaPacketConsumerFull::DemandSet(media::MediaPacketDemandPtr demand) {
  if (terse_) {
    return;
  }
  std::cout << entry() << "MediaPacketConsumer.DemandSet" << std::endl;
  std::cout << indent;
  std::cout << begl << "demand: " << demand;
  std::cout << outdent;
}

void MediaPacketConsumerFull::Reset() {
  std::cout << entry() << "MediaPacketConsumer.Reset" << std::endl;
}

void MediaPacketConsumerFull::Failed() {
  std::cout << entry() << "MediaPacketConsumer.Failed" << std::endl;
}

void MediaPacketConsumerFull::RespondingToGetDemandUpdate(
    media::MediaPacketDemandPtr demand) {
  if (terse_) {
    return;
  }
  std::cout << entry() << "MediaPacketConsumer.RespondingToGetDemandUpdate"
            << std::endl;
  std::cout << indent;
  std::cout << begl << "demand: " << demand;
  std::cout << outdent;
}

void MediaPacketConsumerFull::AddPayloadBufferRequested(uint32_t id,
                                                        uint64_t size) {
  std::cout << entry() << "MediaPacketConsumer.AddPayloadBufferRequested"
            << std::endl;
  std::cout << indent;
  std::cout << begl << "id: " << id << std::endl;
  std::cout << begl << "size: " << size << std::endl;
  std::cout << outdent;
}

void MediaPacketConsumerFull::RemovePayloadBufferRequested(uint32_t id) {
  std::cout << entry() << "MediaPacketConsumer.RemovePayloadBufferRequested"
            << std::endl;
  std::cout << indent;
  std::cout << begl << "id: " << id << std::endl;
  std::cout << outdent;
}

void MediaPacketConsumerFull::FlushRequested() {
  std::cout << entry() << "MediaPacketConsumer.FlushRequested" << std::endl;
}

void MediaPacketConsumerFull::CompletingFlush() {
  std::cout << entry() << "MediaPacketConsumer.CompletingFlush" << std::endl;
}

void MediaPacketConsumerFull::PacketSupplied(uint64_t label,
                                             media::MediaPacketPtr packet,
                                             uint64_t payload_address,
                                             uint32_t packets_outstanding) {
  if (terse_) {
    return;
  }
  std::cout << entry() << "MediaPacketConsumer.PacketSupplied" << std::endl;
  std::cout << indent;
  std::cout << begl << "label: " << label << std::endl;
  std::cout << begl << "packet: " << packet;
  std::cout << begl << "payload_address: " << AsAddress(payload_address)
            << std::endl;
  std::cout << begl << "packets_outstanding: " << packets_outstanding
            << std::endl;
  std::cout << outdent;
}

void MediaPacketConsumerFull::ReturningPacket(uint64_t label,
                                              uint32_t packets_outstanding) {
  if (terse_) {
    return;
  }
  std::cout << entry() << "MediaPacketConsumer.ReturningPacket" << std::endl;
  std::cout << indent;
  std::cout << begl << "label: " << label << std::endl;
  std::cout << begl << "packets_outstanding: " << packets_outstanding
            << std::endl;
  std::cout << outdent;
}

}  // namespace handlers
}  // namespace flog
