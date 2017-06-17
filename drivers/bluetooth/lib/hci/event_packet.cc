// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "event_packet.h"

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

EventPacket::EventPacket(common::ByteBuffer* buffer) : common::Packet<EventHeader>(buffer) {
  SetPayloadSize(GetHeader().parameter_total_size);
  FTL_DCHECK(GetPayloadSize() <= kMaxEventPacketPayloadSize);
}

MutableEventPacket::MutableEventPacket(EventCode event_code, common::MutableByteBuffer* buffer)
    : common::MutablePacket<EventHeader>(buffer, buffer->size() - sizeof(EventHeader)) {
  FTL_DCHECK(buffer->size() >= sizeof(EventHeader));
  FTL_DCHECK(GetPayloadSize() <= kMaxEventPacketPayloadSize);
  GetMutableHeader()->event_code = event_code;
  GetMutableHeader()->parameter_total_size = static_cast<uint8_t>(GetPayloadSize());
}

}  // namespace hci
}  // namespace bluetooth
