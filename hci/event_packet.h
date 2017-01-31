// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/common/packet.h"
#include "apps/bluetooth/hci/hci.h"

namespace bluetooth {

namespace common {
class ByteBuffer;
}  // namespace common

namespace hci {

// Represents a HCI command packet.
class EventPacket : public ::bluetooth::common::Packet<EventHeader> {
 public:
  EventPacket(EventCode event_code,
              common::ByteBuffer* buffer,
              size_t payload_size = 0u);

  // Returns the HCI event code for this packet.
  EventCode event_code() const { return GetHeader().event_code; }

  // common::Packet overrides
  void EncodeHeader() override;

  // Returns the minimum number of bytes needed for an EventPacket with the
  // given |payload_size|.
  constexpr static size_t GetMinBufferSize(size_t payload_size) {
    return sizeof(EventHeader) + payload_size;
  }

  // If this is a CommandComplete event packet, this method returns a pointer to
  // the beginning of the return parameter structure. If the given template type
  // would exceed the bounds of the packet or if this packet does not represent
  // a CommandComplete event, this method returns nullptr.
  template <typename ReturnParams>
  ReturnParams* GetReturnParams() const {
    if (event_code() != kCommandCompleteEventCode ||
        sizeof(ReturnParams) >
            GetPayloadSize() - sizeof(CommandCompleteEventParams))
      return nullptr;
    return reinterpret_cast<ReturnParams*>(
        GetPayload<CommandCompleteEventParams>()->return_parameters);
  }
};

}  // namespace hci
}  // namespace bluetooth
