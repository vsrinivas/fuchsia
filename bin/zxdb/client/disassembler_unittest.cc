// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/bin/zxdb/client/arch_info.h"
#include "garnet/bin/zxdb/client/disassembler.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
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
  std::vector<std::string> out;

  // "int3".
  const uint8_t int3_data[1] = {0xCC};
  size_t consumed = d.DisassembleOne(int3_data, arraysize(int3_data),
                                     0x1234567890, opts, &out);
  EXPECT_EQ(1u, consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ("int3", out[0]);
  EXPECT_EQ("", out[1]);  // Params.
  EXPECT_EQ("", out[2]);  // Comment.

  // "mov edi, 0x28e5e0" with bytes and address.
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};
  out.clear();
  opts.emit_addresses = true;
  opts.emit_bytes = true;
  consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data), 0x1234, opts, &out);
  EXPECT_EQ(5u, consumed);
  EXPECT_EQ(std::vector<std::string>(
                {"0x1234", "bf e0 e5 28 00", "mov", "edi, 0x28e5e0", ""}),
            out);
}

TEST(Disassembler, X64Undecodable) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<std::string> out;

  // This instruction is "mov edi, 0x28e5e0". Cutting this shorter will give
  // undecodable instructions.
  const uint8_t mov_data[5] = {0xbf, 0xe0, 0xe5, 0x28, 0x00};

  // Check with no emitting undecodable.
  opts.emit_undecodable = false;
  size_t consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(0u, consumed);
  EXPECT_TRUE(out.empty());

  // Emit undecodable. On X64 this will consume one byte.
  opts.emit_undecodable = true;
  out.clear();
  consumed =
      d.DisassembleOne(mov_data, arraysize(mov_data) - 1, 0x1234, opts, &out);
  EXPECT_EQ(1u, consumed);
  EXPECT_EQ(
      std::vector<std::string>({".byte", "0xbf", "# Invalid instruction."}),
      out);
}

TEST(Disassembler, X64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  std::vector<std::vector<std::string>> out;

  const uint8_t data[] = {
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea 0xc(%rsp),%rdi
  };

  // Full block.
  size_t consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data), consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(std::vector<std::string>({"mov", "edi, 0x28e5e0", ""}), out[0]);
  EXPECT_EQ(std::vector<std::string>({"mov", "rsi, rbx", ""}), out[1]);
  EXPECT_EQ(std::vector<std::string>({"lea", "rdi, [rsp + 0xc]", ""}), out[2]);

  // Limit the number of instructions.
  out.clear();
  consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 2, &out);
  EXPECT_EQ(8u, consumed);
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(std::vector<std::string>({"mov", "edi, 0x28e5e0", ""}), out[0]);
  EXPECT_EQ(std::vector<std::string>({"mov", "rsi, rbx", ""}), out[1]);

  // Have 3 bytes off the end.
  opts.emit_undecodable = false;  // Should be overridden.
  out.clear();
  consumed =
      d.DisassembleMany(data, arraysize(data) - 3, 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data) - 3, consumed);
  ASSERT_EQ(4u, out.size());
  EXPECT_EQ(std::vector<std::string>({"mov", "edi, 0x28e5e0", ""}), out[0]);
  EXPECT_EQ(std::vector<std::string>({"mov", "rsi, rbx", ""}), out[1]);
  EXPECT_EQ(
      std::vector<std::string>({".byte", "0x48", "# Invalid instruction."}),
      out[2]);
  EXPECT_EQ(
      std::vector<std::string>({".byte", "0x8d", "# Invalid instruction."}),
      out[3]);

  // Add addresses and bytes.
  opts.emit_addresses = true;
  opts.emit_bytes = true;
  out.clear();
  consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data), consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(std::vector<std::string>(
                {"0x123456780", "bf e0 e5 28 00", "mov", "edi, 0x28e5e0", ""}),
            out[0]);
  EXPECT_EQ(std::vector<std::string>(
                {"0x123456785", "48 89 de", "mov", "rsi, rbx", ""}),
            out[1]);
  EXPECT_EQ(std::vector<std::string>({"0x123456788", "48 8d 7c 24 0c", "lea",
                                      "rdi, [rsp + 0xc]", ""}),
            out[2]);
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
  std::vector<std::vector<std::string>> out;

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
  size_t consumed = d.DisassembleDump(dump, opts, 5, &out);
  EXPECT_EQ(21u, consumed);
  ASSERT_EQ(5u, out.size());
  EXPECT_EQ(
      std::vector<std::string>({"0x123456780", "mov", "edi, 0x28e5e0", ""}),
      out[0]);
  EXPECT_EQ(std::vector<std::string>({"0x123456785", "mov", "rsi, rbx", ""}),
            out[1]);
  EXPECT_EQ(
      std::vector<std::string>({"0x123456788", "lea", "rdi, [rsp + 0xc]", ""}),
      out[2]);
  EXPECT_EQ(
      std::vector<std::string>({"0x12345678d", "mov", "edi, 0x28e5e0", ""}),
      out[3]);
  EXPECT_EQ(std::vector<std::string>({"0x123456792", "mov", "rsi, rbx", ""}),
            out[4]);

  // Empty dump (with one block but 0 size).
  out.clear();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>());
  consumed = d.DisassembleDump(dump, opts, 0, &out);
  EXPECT_EQ(0u, consumed);
  EXPECT_EQ(0u, out.size());

  // Test a memory dump that's completely invalid.
  debug_ipc::MemoryBlock invalid_block;
  invalid_block.address = 0x123456780;
  invalid_block.valid = false;
  invalid_block.size = 16;

  out.clear();
  dump = MemoryDump(std::vector<debug_ipc::MemoryBlock>{invalid_block});
  consumed = d.DisassembleDump(dump, opts, 0, &out);
  EXPECT_EQ(invalid_block.size, consumed);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(std::vector<std::string>(
                {"0x123456780", "??", "", "# Invalid memory @ 0x123456780"}),
            out[0]);

  // Test two valid memory blocks with a sandwich of invalid in-between.
  vect.clear();
  vect.push_back(block_with_data);
  vect.push_back(invalid_block);
  vect.push_back(block_with_data);
  vect[0].address = 0x123456780;
  vect[1].address = vect[0].address + vect[0].size;
  vect[2].address = vect[1].address + vect[1].size;
  size_t total_bytes = vect[2].address + vect[2].size - vect[0].address;

  out.clear();
  dump = MemoryDump(std::move(vect));
  consumed = d.DisassembleDump(dump, opts, 0, &out);
  EXPECT_EQ(total_bytes, consumed);
  ASSERT_EQ(7u, out.size());
  EXPECT_EQ(
      std::vector<std::string>({"0x123456780", "mov", "edi, 0x28e5e0", ""}),
      out[0]);
  EXPECT_EQ(std::vector<std::string>({"0x123456785", "mov", "rsi, rbx", ""}),
            out[1]);
  EXPECT_EQ(
      std::vector<std::string>({"0x123456788", "lea", "rdi, [rsp + 0xc]", ""}),
      out[2]);

  EXPECT_EQ(std::vector<std::string>(
                {"0x12345678d", "??", "",
                 "# Invalid memory @ 0x12345678d - 0x12345679c"}),
            out[3]);

  EXPECT_EQ(
      std::vector<std::string>({"0x12345679d", "mov", "edi, 0x28e5e0", ""}),
      out[4]);
  EXPECT_EQ(std::vector<std::string>({"0x1234567a2", "mov", "rsi, rbx", ""}),
            out[5]);
  EXPECT_EQ(
      std::vector<std::string>({"0x1234567a5", "lea", "rdi, [rsp + 0xc]", ""}),
      out[6]);
}

TEST(Disassembler, Arm64Many) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kArm64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&arch);
  ASSERT_FALSE(err.has_error()) << err.msg();

  std::vector<std::vector<std::string>> out;

  const uint8_t data[] = {
      0xf3, 0x0f, 0x1e, 0xf8,  // str x19, [sp, #-0x20]!
      0xfd, 0x7b, 0x01, 0xa9,  // stp x29, x30, [sp, #0x10]
      0xfd, 0x43, 0x00, 0x91   // add x29, sp, #16
  };

  Disassembler::Options opts;
  opts.emit_addresses = true;
  opts.emit_bytes = true;
  size_t consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data), consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(std::vector<std::string>({"0x123456780", "f3 0f 1e f8", "str",
                                      "x19, [sp, #-0x20]!", ""}),
            out[0]);
  EXPECT_EQ(std::vector<std::string>({"0x123456784", "fd 7b 01 a9", "stp",
                                      "x29, x30, [sp, #0x10]", ""}),
            out[1]);
  // LLVM emits a comment "=0x10" here which isn't very helpful. If this
  // changes in a future LLVM update, it's fine.
  EXPECT_EQ(std::vector<std::string>({"0x123456788", "fd 43 00 91", "add",
                                      "x29, sp, #0x10", "// =0x10"}),
            out[2]);

  // Test an instruction off the end.
  out.clear();
  consumed =
      d.DisassembleMany(data, arraysize(data) - 1, 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data) - 1, consumed);
  ASSERT_EQ(3u, out.size());
  EXPECT_EQ(std::vector<std::string>({"0x123456780", "f3 0f 1e f8", "str",
                                      "x19, [sp, #-0x20]!", ""}),
            out[0]);
  EXPECT_EQ(std::vector<std::string>({"0x123456784", "fd 7b 01 a9", "stp",
                                      "x29, x30, [sp, #0x10]", ""}),
            out[1]);
  EXPECT_EQ(
      std::vector<std::string>({"0x123456788", "fd 43 00", ".byte",
                                "0xfd 0x43 0x00", "// Invalid instruction."}),
      out[2]);
}

}  // namespace zxdb
