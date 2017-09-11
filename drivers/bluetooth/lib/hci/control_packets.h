// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <endian.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/packet.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {

using CommandPacket = Packet<CommandHeader>;
using EventPacket = Packet<EventHeader>;

// Packet template specialization for HCI command packets.
template <>
class Packet<CommandHeader> : public PacketBase<CommandHeader, CommandPacket> {
 public:
  // Slab-allocates a new CommandPacket with the given payload size and initializes the packet's
  // header field.
  static std::unique_ptr<CommandPacket> New(OpCode opcode, size_t payload_size = 0u);

  // Returns the HCI command opcode currently in this packet.
  OpCode opcode() const { return le16toh(view().header().opcode); }

 protected:
  Packet<CommandHeader>() = default;

 private:
  // Writes the given header fields into the underlying buffer.
  void WriteHeader(OpCode opcode);
};

// Packet template specialization for HCI event packets.
template <>
class Packet<EventHeader> : public PacketBase<EventHeader, EventPacket> {
 public:
  // Slab-allocates a new EventPacket with the given payload size without initializing its contents.
  static std::unique_ptr<EventPacket> New(size_t payload_size);

  // Returns the HCI event code currently in this packet.
  EventCode event_code() const { return view().header().event_code; }

  // If this is a CommandComplete event packet, this method returns a pointer to
  // the beginning of the return parameter structure. If the given template type
  // would exceed the bounds of the packet or if this packet does not represent
  // a CommandComplete event, this method returns nullptr.
  template <typename ReturnParams>
  const ReturnParams* return_params() const {
    if (event_code() != kCommandCompleteEventCode ||
        sizeof(ReturnParams) > view().payload_size() - sizeof(CommandCompleteEventParams))
      return nullptr;
    return reinterpret_cast<const ReturnParams*>(
        view().payload<CommandCompleteEventParams>().return_parameters);
  }

  // If this is a LE Meta Event packet, this method returns a pointer to the beginning of the
  // subevent parameter structure. If the given template type would exceed the bounds of the packet
  // or if this packet does not represent a LE Meta Event, this method returns nullptr.
  template <typename SubeventParams>
  const SubeventParams* le_event_params() const {
    if (event_code() != kLEMetaEventCode ||
        sizeof(SubeventParams) > view().payload_size() - sizeof(LEMetaEventParams))
      return nullptr;
    return reinterpret_cast<const SubeventParams*>(
        view().payload<LEMetaEventParams>().subevent_parameters);
  }

  // Initializes the internal PacketView by reading the header portion of the underlying buffer.
  void InitializeFromBuffer();

 protected:
  Packet<EventHeader>() = default;
};

}  // namespace hci
}  // namespace bluetooth
