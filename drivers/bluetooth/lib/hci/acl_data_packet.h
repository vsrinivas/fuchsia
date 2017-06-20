// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/common/packet_view.h"
#include "apps/bluetooth/lib/hci/hci.h"

namespace bluetooth {

namespace common {
class ByteBuffer;
class MutableByteBuffer;
}  // namespace common

namespace hci {

// TODO(armansito): Rename these to ACLDataPacketReader and ACLDataPacketWriter.

// Represents a HCI ACL data packet to be sent from the host to the controller.
class ACLDataTxPacket : public ::bluetooth::common::MutablePacketView<ACLDataHeader> {
 public:
  ACLDataTxPacket(ConnectionHandle connection_handle, ACLPacketBoundaryFlag packet_boundary_flag,
                  ACLBroadcastFlag broadcast_flag, size_t data_length,
                  common::MutableByteBuffer* buffer);

  // Encodes the contents of the packet header contents into the underlying buffer.. This method
  // must be called before this packet can be sent to the controller.
  void EncodeHeader();

  // Returns the minimum number of bytes needed for a ACLDataPacket with the given |payload_size|.
  constexpr static size_t GetMinBufferSize(size_t payload_size) {
    return sizeof(ACLDataHeader) + payload_size;
  }

 private:
  ConnectionHandle connection_handle_;
  ACLPacketBoundaryFlag packet_boundary_flag_;
  ACLBroadcastFlag broadcast_flag_;
};

// Represents a HCI ACL data packet received from the controller.
class ACLDataRxPacket : public ::bluetooth::common::PacketView<ACLDataHeader> {
 public:
  explicit ACLDataRxPacket(const common::ByteBuffer* buffer);

  // Getters for header fields.
  ConnectionHandle GetConnectionHandle() const;
  ACLPacketBoundaryFlag GetPacketBoundaryFlag() const;
  ACLBroadcastFlag GetBroadcastFlag() const;
};

}  // namespace hci
}  // namespace bluetooth
