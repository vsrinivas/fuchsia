// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_disassemble.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbDisassemble : public ConsoleTest {};

}  // namespace

TEST_F(VerbDisassemble, Test) {
  mock_remote_api()->AddMemory(
      0x12340,
      {
          0x64, 0x48, 0x8b, 0x04, 0x25, 0x18, 0x00, 0x00, 0x00,  // mov  rax, qword ptr fs:[0x18]
          0x48, 0x89, 0xc1,                                      // mov  rcx, rax
          0x48, 0x81, 0xc1, 0xf0, 0xfa, 0xff, 0xff,              // add  rcx, -0x510
          0x64, 0x48, 0x89, 0x0c, 0x25, 0x18, 0x00, 0x00, 0x00,  // mov  qword ptr fs:[0x18], rcx
          0x48, 0x89, 0xc1,                                      // mov  rcx, rax
          0x48, 0x83, 0xc1, 0xf8,                                // add  rcx, -0x8
          0x64, 0x48, 0x8b, 0x14, 0x25, 0x10, 0x00, 0x00, 0x00,  // mov  rdx, qword ptr fs:[0x10]
          0x48, 0x89, 0x50, 0xf8,                                // mov  qword ptr [rax - 0x8], rdx
          0xc7, 0x45, 0xfc, 0x00, 0x00, 0x00, 0x00               // mov  dword ptr [rbp - 0x4], 0x0
      });

  // Line-limited output with an explicit address.
  console().ProcessInputLine("di -n 3 0x12340");
  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  // NOTE: output has trailing spaces because it's the separator for the comment lines. There
  // are no comments on these lines so it looks weird.
  EXPECT_EQ(
      "   0x12340  mov  rax, qword ptr fs:[0x18] \n"
      "   0x12349  mov  rcx, rax \n"
      "   0x1234c  add  rcx, -0x510 \n",
      event.output.AsString());

  // Default-length output with an expression and data bytes. This should output all of the memory
  // because our data is less than the default line size (16 instructions).
  console().ProcessInputLine("di -r *0x12340 + 9");
  event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "   0x12349  48 89 c1     mov  rcx, rax  \n"
      "   0x1234c  48 81 c1 f0 fa ff ff  add  rcx, -0x510 \n"
      "   0x12353  64 48 89 0c 25 18 00 00 00  mov  qword ptr fs:[0x18], rcx \n"
      "   0x1235c  48 89 c1     mov  rcx, rax  \n"
      "   0x1235f  48 83 c1 f8  add  rcx, -0x8 \n"
      "   0x12363  64 48 8b 14 25 10 00 00 00  mov  rdx, qword ptr fs:[0x10] \n"
      "   0x1236c  48 89 50 f8  mov  qword ptr [rax - 0x8], rdx \n"
      "   0x12370  c7 45 fc 00 00 00 00  mov  dword ptr [rbp - 0x4], 0x0 \n",
      event.output.AsString());
}

}  // namespace zxdb
