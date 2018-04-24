// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "garnet/bin/zxdb/client/disassembler.h"
#include "garnet/bin/zxdb/client/output_buffer.h"
#include "garnet/bin/zxdb/client/session_llvm_state.h"
#include "garnet/public/lib/fxl/arraysize.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Disassembler, X64Individual) {
  SessionLLVMState llvm;
  Err err = llvm.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&llvm);
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
  SessionLLVMState llvm;
  Err err = llvm.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&llvm);
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

TEST(Disassembler, Many) {
  SessionLLVMState llvm;
  Err err = llvm.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler d;
  err = d.Init(&llvm);
  ASSERT_FALSE(err.has_error()) << err.msg();

  Disassembler::Options opts;
  OutputBuffer out;

  const uint8_t data[] = {
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea 0xc(%rsp),%rdi
  };

  // Full block.
  size_t consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data), consumed);
  EXPECT_EQ(
      "\tmov\tedi, 0x28e5e0\n"
      "\tmov\trsi, rbx\n"
      "\tlea\trdi, [rsp + 0xc]\n",
      out.AsString());

  // Limit the number of instructions.
  out = OutputBuffer();
  consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 2, &out);
  EXPECT_EQ(8u, consumed);
  EXPECT_EQ(
      "\tmov\tedi, 0x28e5e0\n"
      "\tmov\trsi, rbx\n",
      out.AsString());

  // Have 3 bytes off the end.
  opts.emit_undecodable = false;  // Should be overridden.
  out = OutputBuffer();
  consumed =
      d.DisassembleMany(data, arraysize(data) - 3, 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data) - 3, consumed);
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
  consumed =
      d.DisassembleMany(data, arraysize(data), 0x123456780, opts, 0, &out);
  EXPECT_EQ(arraysize(data), consumed);
  EXPECT_EQ(
      "\t0x0000000123456780\tbf e0 e5 28 00\tmov\tedi, 0x28e5e0\n"
      "\t0x0000000123456785\t48 89 de\tmov\trsi, rbx\n"
      "\t0x0000000123456788\t48 8d 7c 24 0c\tlea\trdi, [rsp + 0xc]\n",
      out.AsString());
}

}  // namespace zxdb
