// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acl_data_packet.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

ACLDataTxPacket::ACLDataTxPacket(ConnectionHandle connection_handle,
                                 ACLPacketBoundaryFlag packet_boundary_flag,
                                 ACLBroadcastFlag broadcast_flag, size_t data_length,
                                 common::MutableByteBuffer* buffer)
    : common::MutablePacketView<ACLDataHeader>(buffer, data_length),
      connection_handle_(connection_handle),
      packet_boundary_flag_(packet_boundary_flag),
      broadcast_flag_(broadcast_flag) {
  // Must fit inside 12-bits.
  FTL_DCHECK(connection_handle_ <= 0x0FFF);

  // Must fit inside 2-bits.
  FTL_DCHECK(static_cast<uint8_t>(packet_boundary_flag_) <= 0x03);
  FTL_DCHECK(static_cast<uint8_t>(broadcast_flag_) <= 0x03);

  // The maximum ACL data payload length is obtained dynamically from the controller
  // (HCI_LE_Read_Buffer_Size, HCI_Read_Buffer_Size). Here we simply make sure that |data_length|
  // fits inside a uint16_t.
  FTL_DCHECK(data_length <= std::numeric_limits<uint16_t>::max());
}

void ACLDataTxPacket::EncodeHeader() {
  uint16_t handle_and_flags = connection_handle_ |
                              (static_cast<uint16_t>(packet_boundary_flag_) << 12) |
                              (static_cast<uint16_t>(broadcast_flag_) << 14);
  mutable_header()->handle_and_flags = htole16(handle_and_flags);
  mutable_header()->data_total_length = htole16(static_cast<uint16_t>(payload_size()));
}

ACLDataRxPacket::ACLDataRxPacket(const common::ByteBuffer* buffer)
    : common::PacketView<ACLDataHeader>(buffer) {
  Resize(le16toh(header().data_total_length));
}

ConnectionHandle ACLDataRxPacket::GetConnectionHandle() const {
  // Return the lower 12-bits of the first two octets.
  return le16toh(header().handle_and_flags) & 0x0FFF;
}

ACLPacketBoundaryFlag ACLDataRxPacket::GetPacketBoundaryFlag() const {
  // Return bits 4-5 in the higher octet of |handle_and_flags| or "0b00xx000000000000".
  return static_cast<ACLPacketBoundaryFlag>((le16toh(header().handle_and_flags) >> 12) & 0x0003);
}

ACLBroadcastFlag ACLDataRxPacket::GetBroadcastFlag() const {
  // Return bits 6-7 in the higher octet of |handle_and_flags| or "0bxx00000000000000".
  return static_cast<ACLBroadcastFlag>(le16toh(header().handle_and_flags) >> 14);
}

}  // namespace hci
}  // namespace bluetooth
