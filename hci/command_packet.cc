// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_packet.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

CommandPacket::CommandPacket(OpCode opcode,
                             common::ByteBuffer* buffer,
                             size_t payload_size)
    : common::Packet<CommandHeader>(buffer, payload_size), opcode_(opcode) {
  FTL_DCHECK(GetPayloadSize() <= kMaxCommandPacketPayloadSize);
}

void CommandPacket::EncodeHeader() {
  FTL_DCHECK(GetPayloadSize() <= kMaxCommandPacketPayloadSize);
  GetMutableHeader()->opcode = htole16(opcode_);
  GetMutableHeader()->parameter_total_size =
      static_cast<uint8_t>(GetPayloadSize());
}

}  // namespace hci
}  // namespace bluetooth
