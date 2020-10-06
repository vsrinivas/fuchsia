// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <vector>

#include <magma_util/instruction_writer.h>
#include <magma_util/macros.h>

class Packet {
 public:
  static uint32_t parity(uint32_t v) {
    // Based on http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
    // Modified for odd parity
    v ^= v >> 28;
    v ^= v >> 24;
    v ^= v >> 20;
    v ^= v >> 16;
    v ^= v >> 12;
    v ^= v >> 8;
    v ^= v >> 4;
    v &= 0xf;
    return (0x9669 >> v) & 1;
  }
};

// For writing registers
class Packet4 : public Packet {
 public:
  static constexpr uint32_t kPacketType = 0x40000000;

  static void write(magma::InstructionWriter* writer, uint32_t register_index, uint32_t value) {
    DASSERT(!(register_index & 0xFFFC0000));
    uint8_t count = 1;
    uint32_t header = kPacketType;
    header |= count;
    header |= (parity(count) << 7);
    header |= (register_index << 8);
    header |= (parity(register_index) << 27);
    writer->Write32(header);
    writer->Write32(value);
  }
};

// For a range of tasks determined by OpCode.
class Packet7 : public Packet {
 public:
  static constexpr uint32_t kPacketType = 0x70000000;

  enum class OpCode {
    CpRegisterToMemory = 62,
    CpMeInit = 72,
  };

  static void write(magma::InstructionWriter* writer, OpCode opcode,
                    const std::vector<uint32_t>& packet) {
    uint16_t count = static_cast<uint16_t>(packet.size());
    DASSERT(count == packet.size());
    DASSERT(!(count & 0x8000));
    uint32_t op = static_cast<uint32_t>(opcode);
    DASSERT(!(op & 0x80));
    uint32_t header = kPacketType;
    header |= count;
    header |= (parity(count) << 15);
    header |= (op << 16);
    header |= (parity(op) << 23);
    writer->Write32(header);
    for (uint32_t value : packet) {
      writer->Write32(value);
    }
  }
};

#endif  // INSTRUCTIONS_H
