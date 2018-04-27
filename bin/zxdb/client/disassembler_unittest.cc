// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/bin/zxdb/client/arch_info.h"
#include "garnet/bin/zxdb/client/disassembler.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/output_buffer.h"
#include "garnet/public/lib/fxl/arraysize.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Disassembler, X64Individual) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  OutputBuffer out;

  // "int3".
  const uint8_t int3_data[1] = {0xCC};
  size_t consumed = d.DisassembleOne(int3_data, arraysize(int3_data),
                                     0x1234567890, opts, &out);
  EXPECT_EQ(1u, consumed);
  EXPECT_EQ("\tint3\n", out.AsString());

  // "mov edi, 0x28e5e0"
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};
  out = OutputBuffer();
  consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data), 0x1234, opts, &out);
  EXPECT_EQ(5u, consumed);
  EXPECT_EQ("\tmov\tedi, 0x28e5e0\n", out.AsString());
}

TEST(Disassembler, X64Undecodable) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  OutputBuffer out;

  // This instruction is "mov edi, 0x28e5e0". Cutting this shorter will give
  // undecodable instructions.
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};

  // Check with no emitting undecodable.
  opts.emit_undecodable = false;
  size_t consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(0u, consumed);
  EXPECT_EQ("", out.AsString());

  // Emit undecodable. On X64 this will consume one byte.
  opts.emit_undecodable = true;
  consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(1u, consumed);
  EXPECT_EQ("\t.byte\t0xbf\t# Invalid instruction.\n", out.AsString());
}

TEST(Disassembler, X64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  OutputBuffer out;

  const uint8_t data[] = {
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea 0xc(%rsp),%rdi
  };

  // Full block.
  size_t count = 0;
  size_t consumed = d.DisassembleMany(data, arraysize(data), 0x123456780, opts,
                                      0, &out, &count);
  EXPECT_EQ(arraysize(data), consumed);
  EXPECT_EQ(3u, count);
  EXPECT_EQ(
      "\tmov\tedi, 0x28e5e0\n"
      "\tmov\trsi, rbx\n"
      "\tlea\trdi, [rsp + 0xc]\n",
      out.AsString());

  // Limit the number of instructions.
  out = OutputBuffer();
  consumed = d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 2,
                               &out, &count);
  EXPECT_EQ(8u, consumed);
  EXPECT_EQ(2u, count);
  EXPECT_EQ(
      "\tmov\tedi, 0x28e5e0\n"
      "\tmov\trsi, rbx\n",
      out.AsString());

  // Have 3 bytes off the end.
  opts.emit_undecodable = false;  // Should be overridden.
  out = OutputBuffer();
  consumed = d.DisassembleMany(data, arraysize(data) - 3, 0x123456780, opts, 0,
                               &out, &count);
  EXPECT_EQ(arraysize(data) - 3, consumed);
  EXPECT_EQ(4u, count);
  EXPECT_EQ(
      "\tmov\tedi, 0x28e5e0\n"
      "\tmov\trsi, rbx\n"
      "\t.byte\t0x48\t# Invalid instruction.\n"
      "\t.byte\t0x8d\t# Invalid instruction.\n",
      out.AsString());

  // Add addresses and bytes.
  opts.emit_addresses = true;
  opts.emit_bytes = true;
  out = OutputBuffer();
  consumed = d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0,
                               &out, &count);
  EXPECT_EQ(arraysize(data), consumed);
  EXPECT_EQ(3u, count);
  EXPECT_EQ(
      "\t0x0000000123456780\tbf e0 e5 28 00\tmov\tedi, 0x28e5e0\n"
      "\t0x0000000123456785\t48 89 de\tmov\trsi, rbx\n"
      "\t0x0000000123456788\t48 8d 7c 24 0c\tlea\trdi, [rsp + 0xc]\n",
      out.AsString());
}

TEST(Disassembler, Dump) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  opts.emit_addresses = true;
  OutputBuffer out;

  // Make a little memory block with valid instructions in it.
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = 0;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea 0xc(%rsp),%rdi
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());

  // Two valid memory regions that just follow on each other. This sets a
  // limit on the total instructions.
  std::vector<debug_ipc::MemoryBlock> vect;
  vect.push_back(block_with_data);
  vect.push_back(block_with_data);
  vect[0].address = 0x123456780;
  vect[1].address = vect[0].address + vect[0].size;

  MemoryDump dump(std::move(vect));
  size_t count = 0;
  size_t consumed = d.DisassembleDump(dump, opts, 5, &out, &count);
  EXPECT_EQ(21u, consumed);
  EXPECT_EQ(5u, count);
  EXPECT_EQ(
      "\t0x0000000123456780\tmov\tedi, 0x28e5e0\n"
      "\t0x0000000123456785\tmov\trsi, rbx\n"
      "\t0x0000000123456788\tlea\trdi, [rsp + 0xc]\n"
      "\t0x000000012345678d\tmov\tedi, 0x28e5e0\n"
      "\t0x0000000123456792\tmov\trsi, rbx\n",
      out.AsString());

  // Empty dump (with one block but 0 size).
  out = OutputBuffer();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>());
  consumed = d.DisassembleDump(dump, opts, 0, &out, &count);
  EXPECT_EQ(0u, consumed);
  EXPECT_EQ(0u, count);
  EXPECT_EQ("", out.AsString());

  // Test a memory dump that's completely invalid.
  debug_ipc::MemoryBlock invalid_block;
  invalid_block.address = 0x123456780;
  invalid_block.valid = false;
  invalid_block.size = 16;

  out = OutputBuffer();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>{invalid_block});
  consumed = d.DisassembleDump(dump, opts, 0, &out, &count);
  EXPECT_EQ(invalid_block.size, consumed);
  EXPECT_EQ(1u, count);
  EXPECT_EQ("\t0x0000000123456780\t??\t# Invalid memory.\n", out.AsString());

  // Test two valid memory blocks with a sandwich of invalid in-between.
  vect.clear();
  vect.push_back(block_with_data);
  vect.push_back(invalid_block);
  vect.push_back(block_with_data);
  vect[0].address = 0x123456780;
  vect[1].address = vect[0].address + vect[0].size;
  vect[2].address = vect[1].address + vect[1].size;
  size_t total_bytes = vect[2].address + vect[2].size - vect[0].address;

  out = OutputBuffer();
  dump = MemoryDump(std::move(vect));
  consumed = d.DisassembleDump(dump, opts, 0, &out, &count);
  EXPECT_EQ(total_bytes, consumed);
  EXPECT_EQ(7u, count);
  EXPECT_EQ(
      "\t0x0000000123456780\tmov\tedi, 0x28e5e0\n"
      "\t0x0000000123456785\tmov\trsi, rbx\n"
      "\t0x0000000123456788\tlea\trdi, [rsp + 0xc]\n"
      "\t0x000000012345678d - 0x000000012345679c\t??\t# Invalid memory.\n"
      "\t0x000000012345679d\tmov\tedi, 0x28e5e0\n"
      "\t0x00000001234567a2\tmov\trsi, rbx\n"
      "\t0x00000001234567a5\tlea\trdi, [rsp + 0xc]\n",
      out.AsString());
}

TEST(Disassembler, Arm64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kArm64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;

  const uint8_t data[] = {
    0xf3, 0x0f, 0x1e, 0xf8,  // str x19, [sp, #-0x20]!
    0xfd, 0x7b, 0x01, 0xa9,  // stp x29, x30, [sp, #0x10]
    0xfd, 0x43, 0x00, 0x91   // add x29, sp, #16
  };

  Disassembler::Options opts;
  opts.emit_addresses = true;
  opts.emit_bytes = true;
  out = OutputBuffer();
  size_t count = 0;
  size_t consumed = d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0,
                               &out, &count);
  EXPECT_EQ(arraysize(data), consumed);
  EXPECT_EQ(3u, count);
  EXPECT_EQ(
      "\t0x0000000123456780\tf3 0f 1e f8\tstr\tx19, [sp, #-0x20]!\n"
      "\t0x0000000123456784\tfd 7b 01 a9\tstp\tx29, x30, [sp, #0x10]\n"
      "\t0x0000000123456788\tfd 43 00 91\tadd\tx29, sp, #0x10\t// =0x10\n",
      out.AsString());

  // Test an instruction off the end.
  out = OutputBuffer();
  consumed = d.DisassembleMany(data, arraysize(data) - 1, 0x123456780, opts, 0,
                               &out, &count);
  EXPECT_EQ(arraysize(data) - 1, consumed);
  EXPECT_EQ(3u, count);
  EXPECT_EQ(
      "\t0x0000000123456780\tf3 0f 1e f8\tstr\tx19, [sp, #-0x20]!\n"
      "\t0x0000000123456784\tfd 7b 01 a9\tstp\tx29, x30, [sp, #0x10]\n"
      "\t0x0000000123456788\tfd 43 00\t.byte\t0xfd 0x43 0x00\t// Invalid instruction.\n",
      out.AsString());
}


}  // namespace zxdb
