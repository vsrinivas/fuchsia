// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "event_packet.h"

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

EventPacket::EventPacket(common::ByteBuffer* buffer) : common::PacketView<EventHeader>(buffer) {
  Resize(header().parameter_total_size);
  FTL_DCHECK(payload_size() <= kMaxEventPacketPayloadSize);
}

MutableEventPacket::MutableEventPacket(EventCode event_code, common::MutableByteBuffer* buffer)
    : common::MutablePacketView<EventHeader>(buffer, buffer->size() - sizeof(EventHeader)) {
  FTL_DCHECK(buffer->size() >= sizeof(EventHeader));
  FTL_DCHECK(payload_size() <= kMaxEventPacketPayloadSize);
  mutable_header()->event_code = event_code;
  mutable_header()->parameter_total_size = static_cast<uint8_t>(payload_size());
}

}  // namespace hci
}  // namespace bluetooth
