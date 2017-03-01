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
class CommandPacket : public ::bluetooth::common::Packet<CommandHeader> {
 public:
  CommandPacket(OpCode opcode, common::MutableByteBuffer* buffer, size_t payload_size = 0u);

  // Returns the HCI command opcode for this packet.
  OpCode opcode() const { return opcode_; }

  // common::Packet overrides
  void EncodeHeader() override;

  // Returns the minimum number of bytes needed for a CommandPacket with the
  // given |payload_size|.
  constexpr static size_t GetMinBufferSize(size_t payload_size) {
    return sizeof(CommandHeader) + payload_size;
  }

 private:
  OpCode opcode_;
};

}  // namespace hci
}  // namespace bluetooth
