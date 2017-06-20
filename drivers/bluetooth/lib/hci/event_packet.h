// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/common/packet_view.h"
#include "apps/bluetooth/lib/hci/hci.h"

namespace bluetooth {

namespace common {
class ByteBuffer;
}  // namespace common

namespace hci {

// Represents a HCI event packet.
class EventPacket : public ::bluetooth::common::PacketView<EventHeader> {
 public:
  explicit EventPacket(common::ByteBuffer* buffer);

  // Returns the HCI event code for this packet.
  EventCode event_code() const { return header().event_code; }

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
  const ReturnParams* GetReturnParams() const {
    if (event_code() != kCommandCompleteEventCode ||
        sizeof(ReturnParams) > payload_size() - sizeof(CommandCompleteEventParams))
      return nullptr;
    return reinterpret_cast<const ReturnParams*>(
        payload<CommandCompleteEventParams>().return_parameters);
  }

  // If this is a LE Meta Event packet, this method returns a pointer to the beginning of the
  // subevent parameter structure. If the given template type would exceed the bounds of the packet
  // or if this packet does not represent a LE Meta Event, this method returns nullptr.
  template <typename SubeventParams>
  const SubeventParams* GetLEEventParams() const {
    if (event_code() != kLEMetaEventCode ||
        sizeof(SubeventParams) > payload_size() - sizeof(LEMetaEventParams))
      return nullptr;
    return reinterpret_cast<const SubeventParams*>(
        payload<LEMetaEventParams>().subevent_parameters);
  }
};

// An EventPacket that allows its contents to be modified.
class MutableEventPacket : public ::bluetooth::common::MutablePacketView<EventHeader> {
 public:
  // Constructor immediately encodes the event header.
  explicit MutableEventPacket(EventCode event_code, common::MutableByteBuffer* buffer);
};

}  // namespace hci
}  // namespace bluetooth
