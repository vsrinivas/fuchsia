// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/disassembler.h"

#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"

namespace zxdb {

using Row = Disassembler::Row;

TEST(Disassembler, X64Individual) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  Row out;

  // "int3".
  const uint8_t int3_data[1] = {0xCC};
  size_t consumed = d.DisassembleOne(int3_data, std::size(int3_data), 0x1234567890, opts, &out);
  EXPECT_EQ(1u, consumed);
  EXPECT_EQ(std::vector<uint8_t>({0xcc}), out.bytes);
  EXPECT_EQ("int3", out.op);
  EXPECT_EQ("", out.params);   // Params.
  EXPECT_EQ("", out.comment);  // Comment.

  // "mov edi, 0x28e5e0" with bytes and address.
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};
  consumed = d.DisassembleOne(mov_data, std::size(mov_data), 0x1234, opts, &out);
  EXPECT_EQ(5u, consumed);
  EXPECT_EQ(std::vector<uint8_t>({0xbf, 0xe0, 0xe5, 0x28, 0x00}), out.bytes);
  EXPECT_EQ("mov", out.op);
  EXPECT_EQ("edi, 0x28e5e0", out.params);  // Params.
  EXPECT_EQ("", out.comment);              // Comment.
}

TEST(Disassembler, X64Undecodable) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  Row out;

  // This instruction is "mov edi, 0x28e5e0". Cutting this shorter will give
  // undecodable instructions.
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};

  // Check with no emitting undecodable.
  opts.emit_undecodable = false;
  size_t consumed = d.DisassembleOne(mov_data, std::size(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(0u, consumed);
  EXPECT_TRUE(out.op.empty());

  // Emit undecodable. On X64 this will consume one byte.
  opts.emit_undecodable = true;
  consumed = d.DisassembleOne(mov_data, std::size(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(1u, consumed);
  EXPECT_EQ(std::vector<uint8_t>({0xbf}), out.bytes);
  EXPECT_EQ(".byte", out.op);
  EXPECT_EQ("0xbf", out.params);
  EXPECT_EQ("# Invalid instruction.", out.comment);
}

TEST(Disassembler, X64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<Row> out;

  const uint8_t data[] = {
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea rdi, [rsp + 0xc]
  };

  // Full block.
  size_t consumed = d.DisassembleMany(data, std::size(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(std::size(data), consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(Row(0x123456780, &data[0], 5, "mov", "edi, 0x28e5e0", ""), out[0]);
  EXPECT_EQ(Row(0x123456785, &data[5], 3, "mov", "rsi, rbx", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &data[8], 5, "lea", "rdi, [rsp + 0xc]", ""), out[2]);

  // Limit the number of instructions.
  out.clear();
  consumed = d.DisassembleMany(data, std::size(data), 0x123456780, opts, 2, &out);
  EXPECT_EQ(8u, consumed);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(Row(0x123456780, &data[0], 5, "mov", "edi, 0x28e5e0", ""), out[0]);
  EXPECT_EQ(Row(0x123456785, &data[5], 3, "mov", "rsi, rbx", ""), out[1]);

  // Have 3 bytes off the end.
  opts.emit_undecodable = false;  // Should be overridden.
  out.clear();
  consumed = d.DisassembleMany(data, std::size(data) - 3, 0x123456780, opts, 0, &out);
  EXPECT_EQ(std::size(data) - 3, consumed);
  ASSERT_EQ(4u, out.size());
  EXPECT_EQ(Row(0x123456780, &data[0], 5, "mov", "edi, 0x28e5e0", ""), out[0]);
  EXPECT_EQ(Row(0x123456785, &data[5], 3, "mov", "rsi, rbx", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &data[8], 1, ".byte", "0x48", "# Invalid instruction."), out[2]);
  EXPECT_EQ(Row(0x123456789, &data[9], 1, ".byte", "0x8d", "# Invalid instruction."), out[3]);
}

TEST(Disassembler, Dump) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<Row> out;

  // Make a little memory block with valid instructions in it.
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = 0;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea rdi, [rsp + 0xc]
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());

  // Two valid memory regions that just follow on each other. This sets a limit on the total
  // instructions.
  std::vector<debug_ipc::MemoryBlock> vect;
  vect.push_back(block_with_data);
  vect.push_back(block_with_data);
  constexpr uint64_t start_address = 0x123456780;
  vect[0].address = start_address;
  vect[1].address = vect[0].address + vect[0].size;

  MemoryDump dump(std::move(vect));
  size_t consumed = d.DisassembleDump(dump, start_address, opts, 5, &out);
  EXPECT_EQ(21u, consumed);
  ASSERT_EQ(5u, out.size());
  EXPECT_EQ(Row(0x123456780, &block_with_data.data[0], 5, "mov", "edi, 0x28e5e0", ""), out[0]);
  EXPECT_EQ(Row(0x123456785, &block_with_data.data[5], 3, "mov", "rsi, rbx", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &block_with_data.data[8], 5, "lea", "rdi, [rsp + 0xc]", ""), out[2]);
  EXPECT_EQ(Row(0x12345678d, &block_with_data.data[0], 5, "mov", "edi, 0x28e5e0", ""), out[3]);
  EXPECT_EQ(Row(0x123456792, &block_with_data.data[5], 3, "mov", "rsi, rbx", ""), out[4]);

  // Empty dump (with one block but 0 size).
  out.clear();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>());
  consumed = d.DisassembleDump(dump, start_address, opts, 0, &out);
  EXPECT_EQ(0u, consumed);
  EXPECT_EQ(0u, out.size());

  // Test a memory dump that's completely invalid.
  debug_ipc::MemoryBlock invalid_block;
  invalid_block.address = start_address;
  invalid_block.valid = false;
  invalid_block.size = 16;

  out.clear();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>{invalid_block});
  consumed = d.DisassembleDump(dump, start_address, opts, 0, &out);
  EXPECT_EQ(invalid_block.size, consumed);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(Row(start_address, nullptr, 0, "??", "", "# Invalid memory @ 0x123456780"), out[0]);

  // Test two valid memory blocks with a sandwich of invalid in-between.
  vect.clear();
  vect.push_back(block_with_data);
  vect.push_back(invalid_block);
  vect.push_back(block_with_data);
  vect[0].address = start_address;
  vect[1].address = vect[0].address + vect[0].size;
  vect[2].address = vect[1].address + vect[1].size;
  size_t total_bytes = vect[2].address + vect[2].size - vect[0].address;

  out.clear();
  dump = MemoryDump(std::move(vect));
  consumed = d.DisassembleDump(dump, start_address, opts, 0, &out);
  EXPECT_EQ(total_bytes, consumed);
  ASSERT_EQ(7u, out.size());
  EXPECT_EQ(Row(0x123456780, &block_with_data.data[0], 5, "mov", "edi, 0x28e5e0", ""), out[0]);
  EXPECT_EQ(Row(0x123456785, &block_with_data.data[5], 3, "mov", "rsi, rbx", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &block_with_data.data[8], 5, "lea", "rdi, [rsp + 0xc]", ""), out[2]);
  EXPECT_EQ(Row(0x12345678d, nullptr, 0, "??", "", "# Invalid memory @ 0x12345678d - 0x12345679c"),
            out[3]);
  EXPECT_EQ(Row(0x12345679d, &block_with_data.data[0], 5, "mov", "edi, 0x28e5e0", ""), out[4]);
  EXPECT_EQ(Row(0x1234567a2, &block_with_data.data[5], 3, "mov", "rsi, rbx", ""), out[5]);
  EXPECT_EQ(Row(0x1234567a5, &block_with_data.data[8], 5, "lea", "rdi, [rsp + 0xc]", ""), out[6]);
}

TEST(Disassembler, Arm64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kArm64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  std::vector<Row> out;

  const uint8_t data[] = {
      0xf3, 0x0f, 0x1e, 0xf8,  // str x19, [sp, #-0x20]!
      0xfd, 0x7b, 0x01, 0xa9,  // stp x29, x30, [sp, #0x10]
      0xfd, 0x43, 0x00, 0x91   // add x29, sp, #16
  };

  Disassembler::Options opts;
  size_t consumed = d.DisassembleMany(data, std::size(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(std::size(data), consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(Row(0x123456780, &data[0], 4, "str", "x19, [sp, #-0x20]!", ""), out[0]);
  EXPECT_EQ(Row(0x123456784, &data[4], 4, "stp", "x29, x30, [sp, #0x10]", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &data[8], 4, "add", "x29, sp, #0x10", ""), out[2]);

  // Test an instruction off the end.
  out.clear();
  consumed = d.DisassembleMany(data, std::size(data) - 1, 0x123456780, opts, 0, &out);
  EXPECT_EQ(std::size(data) - 1, consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(Row(0x123456780, &data[0], 4, "str", "x19, [sp, #-0x20]!", ""), out[0]);
  EXPECT_EQ(Row(0x123456784, &data[4], 4, "stp", "x29, x30, [sp, #0x10]", ""), out[1]);
  EXPECT_EQ(Row(0x123456788, &data[8], 3, ".byte", "0xfd 0x43 0x00", "// Invalid instruction."),
            out[2]);
}

TEST(Disassembler, CallX64) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<Row> out;

  // Make a little memory block with valid instructions in it.
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = 0x123456780;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0xe8, 0xce, 0x00, 0x00, 0x00,  // call +0xce (relative to next instruction).
      0xe8, 0xf4, 0xff, 0xff, 0xff,  // call -0x0c (relative to next instruction).
      0xff, 0xd0                     // call rax (indirect call to register value).
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());

  std::vector<debug_ipc::MemoryBlock> vect;
  vect.push_back(block_with_data);

  MemoryDump dump(std::move(vect));
  size_t consumed = d.DisassembleDump(dump, block_with_data.address, opts, 0, &out);
  EXPECT_EQ(12u, consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(Row(0x123456780, &block_with_data.data[0], 5, "call", "0xce", "",
                Disassembler::InstructionType::kCallDirect,
                block_with_data.address + 5 /* = length of instruction */ + 0xce),
            out[0]);
  EXPECT_EQ(Row(0x123456785, &block_with_data.data[5], 5, "call", "-0xc", "",
                Disassembler::InstructionType::kCallDirect, block_with_data.address + 10 - 12),
            out[1]);
  EXPECT_EQ(Row(0x12345678a, &block_with_data.data[10], 2, "call", "rax", "",
                Disassembler::InstructionType::kCallIndirect),
            out[2]);
}

TEST(Disassembler, CallArm64) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kArm64, 4096);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<Row> out;

  // Make a little memory block with valid instructions in it.
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = 0xc55f8;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x94,  // bl +0x06 (relative to this instruction)
      0x00, 0x00, 0x3f, 0xd6,  // blr x0
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());

  std::vector<debug_ipc::MemoryBlock> vect;
  vect.push_back(block_with_data);

  MemoryDump dump(std::move(vect));
  size_t consumed = d.DisassembleDump(dump, block_with_data.address, opts, 4, &out);
  EXPECT_EQ(8u, consumed);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(Row(0xc55f8, &block_with_data.data[0], 4, "bl", "#0x18", "",
                Disassembler::InstructionType::kCallDirect, 0xc5610),
            out[0]);
  EXPECT_EQ(Row(0xc55fc, &block_with_data.data[4], 4, "blr", "x0", "",
                Disassembler::InstructionType::kCallIndirect),
            out[1]);
  *out[0].call_dest;
}

}  // namespace zxdb
