// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_context.h"
#include "garnet/bin/zxdb/client/arch_info.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

static const char kSimpleProgram[] =
    R"(#include "foo.h"

int main(int argc, char** argv) {
  printf("Hello, world");
  return 1;
}
)";

TEST(FormatContext, FormatSourceContext) {
  FormatSourceOpts opts;
  opts.first_line = 2;
  opts.last_line = 6;
  opts.active_line = 4;
  opts.highlight_line = 4;
  opts.highlight_column = 11;

  OutputBuffer out;
  ASSERT_FALSE(
      FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   2 \n   3 int main(int argc, char** argv) {\n"
      " ▶ 4   printf(\"Hello, world\");\n"
      "   5   return 1;\n"
      "   6 }\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_OffBeginning) {
  FormatSourceOpts opts;
  opts.first_line = 0;
  opts.last_line = 4;
  opts.active_line = 2;
  opts.highlight_line = 2;
  opts.highlight_column = 11;

  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line
  // at the beginning because it hit the top of the file.
  ASSERT_FALSE(
      FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   1 #include \"foo.h\"\n"
      " ▶ 2 \n"
      "   3 int main(int argc, char** argv) {\n"
      "   4   printf(\"Hello, world\");\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_OffEnd) {
  FormatSourceOpts opts;
  opts.first_line = 4;
  opts.last_line = 8;
  opts.active_line = 6;
  opts.highlight_line = 6;
  opts.highlight_column = 6;

  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line
  // at the beginning because it hit the top of the file.
  ASSERT_FALSE(
      FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   4   printf(\"Hello, world\");\n"
      "   5   return 1;\n"
      " ▶ 6 }\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_LineOffEnd) {
  FormatSourceOpts opts;
  opts.first_line = 0;
  opts.last_line = 100;
  opts.active_line = 10;  // This line is off the end of the input.
  opts.highlight_line = 10;
  opts.require_active_line = true;

  OutputBuffer out;
  Err err = FormatSourceContext("file.cc", kSimpleProgram, opts, &out);
  ASSERT_TRUE(err.has_error());
  EXPECT_EQ("There is no line 10 in the file file.cc", err.msg());
}

TEST(FormatContext, FormatAsmContext) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error());

  // Make a little memory dump.
  constexpr uint64_t start_address = 0x123456780;
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = start_address;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c   // lea rdi, [rsp + 0xc]
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());
  MemoryDump dump(std::vector<debug_ipc::MemoryBlock>({block_with_data}));

  FormatAsmOpts opts;
  opts.emit_addresses = true;
  opts.emit_bytes = false;
  opts.active_address = 0x123456785;
  opts.max_instructions = 100;
  opts.bp_addrs[start_address] = true;

  OutputBuffer out;
  err = FormatAsmContext(&arch, dump, opts, &out);
  ASSERT_FALSE(err.has_error());

  EXPECT_EQ(
      " ◉ 0x123456780  mov  edi, 0x28e5e0 \n"
      " ▶ 0x123456785  mov  rsi, rbx \n"
      "   0x123456788  lea  rdi, [rsp + 0xc] \n",
      out.AsString());

  // Try again with source bytes and a disabled breakpoint on the same line as
  // the active address.
  out = OutputBuffer();
  opts.emit_bytes = true;
  opts.bp_addrs.clear();
  opts.bp_addrs[opts.active_address] = false;
  err = FormatAsmContext(&arch, dump, opts, &out);
  ASSERT_FALSE(err.has_error());

  EXPECT_EQ(
      "   0x123456780  bf e0 e5 28 00  mov  edi, 0x28e5e0 \n"
      "◯▶ 0x123456785  48 89 de        mov  rsi, rbx \n"
      "   0x123456788  48 8d 7c 24 0c  lea  rdi, [rsp + 0xc] \n",
      out.AsString());
}

}  // namespace zxdb
