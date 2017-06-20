// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_packet.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

CommandPacket::CommandPacket(OpCode opcode, common::MutableByteBuffer* buffer, size_t payload_size)
    : common::MutablePacketView<CommandHeader>(buffer, payload_size), opcode_(opcode) {
  FTL_DCHECK(this->payload_size() <= kMaxCommandPacketPayloadSize);
}

CommandPacket::CommandPacket(common::MutableByteBuffer* buffer)
    : common::MutablePacketView<CommandHeader>(buffer, buffer->size() - sizeof(CommandHeader)) {
  FTL_DCHECK(buffer->size() >= sizeof(CommandHeader));
  FTL_DCHECK(payload_size() <= kMaxCommandPacketPayloadSize);
  opcode_ = le16toh(header().opcode);
}

void CommandPacket::EncodeHeader() {
  FTL_DCHECK(payload_size() <= kMaxCommandPacketPayloadSize);
  mutable_header()->opcode = htole16(opcode_);
  mutable_header()->parameter_total_size = static_cast<uint8_t>(payload_size());
}

}  // namespace hci
}  // namespace bluetooth
