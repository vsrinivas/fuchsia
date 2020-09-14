// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/analyze_memory.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

namespace {

using ::zxdb::internal::MemoryAnalysis;
using namespace debug_ipc;

class AnalyzeMemoryTest : public TestWithLoop {};

}  // namespace

TEST_F(AnalyzeMemoryTest, Basic) {
  Session session;
  ProcessSymbolsTestSetup setup;
  MockProcess process(&session);
  process.set_symbols(&setup.process());

  constexpr uint64_t kBegin = 0x1000;
  constexpr uint32_t kLen = 24;  // 3 lines of output (8 bytes each).

  AnalyzeMemoryOptions opts;
  opts.process = &process;
  opts.begin_address = kBegin;
  opts.bytes_to_read = kLen;

  // The callback just saves the buffer to "output".
  OutputBuffer output;
  auto analysis = fxl::MakeRefCounted<MemoryAnalysis>(
      opts, [&output](const Err& err, OutputBuffer analysis, uint64_t next_addr) {
        output = analysis;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  // Setup address space. Make one region inside another. The innermost one should be the one
  // reported.
  std::vector<debug_ipc::AddressRegion> aspace;
  aspace.resize(2);
  aspace[0].name = "root";
  aspace[0].base = 0x1000;
  aspace[0].size = 0x800000000000;
  aspace[0].depth = 0;
  aspace[1].name = "inner";
  aspace[1].base = 0x1000;
  aspace[1].size = 0x1000;
  aspace[1].depth = 1;
  analysis->SetAspace(aspace);

  const uint64_t kStack0SP = kBegin;
  const uint64_t kStack1SP = kBegin + 8;

  constexpr uint64_t kAway = 0xFF00000000000;  // Points out of the dump.
  std::vector<Register> frame0_regs = {Register(RegisterID::kX64_rax, kBegin),
                                       Register(RegisterID::kX64_rcx, kAway),
                                       Register(RegisterID::kX64_rsp, kStack0SP)};

  // Frame 1 duplicates rax (should not have both in the output), but rcx is different and this
  // should be called out in the dump.
  std::vector<Register> frame1_regs = {Register(RegisterID::kX64_rax, kBegin),
                                       Register(RegisterID::kX64_rcx, kBegin + 16),
                                       Register(RegisterID::kX64_rsp, kStack1SP)};

  // Setup frames. This creates a top frame, an intermediate inline frame, and a bottom frame.
  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr,
                                               Location(Location::State::kSymbolized, 0x1234),
                                               kStack0SP, 0, frame0_regs, kStack0SP));
  auto bottom_frame =
      std::make_unique<MockFrame>(nullptr, nullptr, Location(Location::State::kSymbolized, 0x1200),
                                  kStack1SP, 0, frame1_regs, kStack1SP);
  // Inline frame (needs to reference the bottom frame below it).
  frames.push_back(
      std::make_unique<MockFrame>(nullptr, nullptr, Location(Location::State::kSymbolized, 0x1210),
                                  kStack1SP, 0, frame1_regs, kStack1SP, bottom_frame.get()));
  frames.push_back(std::move(bottom_frame));

  // Stack to hold our mock frames. This stack doesn't need to do anything other than return the
  // frames again, so the delegate can be null.
  Stack temp_stack(nullptr);
  temp_stack.SetFramesForTest(std::move(frames), true);
  analysis->SetStack(temp_stack);

  // Setup memory.
  std::vector<debug_ipc::MemoryBlock> blocks;
  blocks.resize(1);
  blocks[0].address = kBegin;
  blocks[0].valid = true;
  blocks[0].size = 0x24;
  blocks[0].data = {
      0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Points to inner.
      0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,  // Inside outer.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Nothing
  };
  analysis->SetMemory(MemoryDump(std::move(blocks)));

  analysis->Schedule(opts);
  debug_ipc::MessageLoop::Current()->Run();

  // The pointer to "inner" aspace entry should be annotated. The "outer" aspace entry is too large
  // and so will be omitted.
  //
  // The "frame 2" registers should be omitted because they were covered by the inline "frame 1"
  // registers above it.
  EXPECT_EQ(
      "Address               Data \n"
      " 0x1000 0x0000000000001000 ◁ rax, rsp, frame 0 base. ▷ inside map "
      "\"inner\"\n"
      " 0x1008 0x0000000010000000 ◁ frame 1 rsp, frame 1 base\n"
      " 0x1010 0x0000000000000000 ◁ frame 1 rcx\n",
      output.AsString());
}

}  // namespace zxdb
