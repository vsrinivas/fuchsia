// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <stdint.h>

#include <magma_util/instruction_writer.h>
#include <magma_util/macros.h>

// The number of dwords per instruction.
static const uint32_t kInstructionDwords = 2;
static const uint32_t kWaitLinkDwords = 2 * kInstructionDwords;

class MiWait {
 public:
  static constexpr uint32_t kCommandType = 0x38000000;

  static void write(magma::InstructionWriter* writer) {
    writer->Write32(kCommandType | 200);
    writer->Write32(0);
  }
};

class MiLink {
 public:
  static constexpr uint32_t kCommandType = 0x40000000;

  static void write(magma::InstructionWriter* writer, uint16_t prefetch, uint32_t addr) {
    writer->Write32(kCommandType | prefetch);
    writer->Write32(addr);
  }
};

class MiLoadState {
 public:
  static constexpr uint32_t kCommandType = 0x8000000;
  static constexpr uint32_t kRegMask = 0xffff;

  static void write(magma::InstructionWriter* writer, uint32_t reg, uint32_t value) {
    uint32_t count = 1 << 16;
    uint32_t reg_index = (reg >> 2) & kRegMask;
    writer->Write32(kCommandType | count | reg_index);
    writer->Write32(value);
  }
};

class MiEvent {
 public:
  static constexpr uint32_t kEventReg = 0x3804;
  static constexpr uint32_t kEventIdMask = 0x1f;
  static constexpr uint32_t kEventFromPE = 0x40;

  static void write(magma::InstructionWriter* writer, uint32_t event_id) {
    DASSERT(event_id <= kEventIdMask);
    MiLoadState::write(writer, kEventReg, event_id | kEventFromPE);
  }
};

class MiEnd {
 public:
  static constexpr uint32_t kCommandType = 0x10000000;

  static void write(magma::InstructionWriter* writer) {
    writer->Write32(kCommandType);
    writer->Write32(0);  // Keep 8-byte aligned.
  }
};

// Used for writing instructions into command buffers.
class BufferWriter : public magma::InstructionWriter {
 public:
  BufferWriter(uint32_t* buf_cpu_addr, uint32_t size, uint32_t offset)
      : buf_cpu_addr_(buf_cpu_addr), size_(size), offset_(offset) {}

  void Write32(uint32_t value) override {
    DASSERT(buf_cpu_addr_);
    DASSERT(offset_ + sizeof(value) <= size_);
    buf_cpu_addr_[offset_ >> 2] = value;
    offset_ += sizeof(value);
    bytes_written_ += sizeof(value);
  }

  uint64_t bytes_written() { return bytes_written_; }

 private:
  uint32_t* buf_cpu_addr_ = nullptr;
  uint32_t size_ = 0;
  uint32_t offset_ = 0;
  uint64_t bytes_written_ = 0;
};

#endif  // INSTRUCTIONS_H
