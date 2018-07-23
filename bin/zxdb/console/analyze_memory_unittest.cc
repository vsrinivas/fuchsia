// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/analyze_memory.h"
#include "garnet/bin/zxdb/client/mock_frame.h"
#include "garnet/bin/zxdb/client/mock_process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/mock_process_symbols.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

using ::zxdb::internal::MemoryAnalysis;
using namespace debug_ipc;

// Provides just enough of a Process implementation for the analyzer to run.
// It just needs a ProcessSymbols implementation.
class MyMockProcess : public MockProcess {
 public:
  explicit MyMockProcess(Session* session) : MockProcess(session) {}

  // Process overrides:
  ProcessSymbols* GetSymbols() override { return &symbols_; }

 private:
  MockProcessSymbols symbols_;
};

class AnalyzeMemoryTest : public testing::Test {
 public:
  AnalyzeMemoryTest() { loop_.Init(); }
  ~AnalyzeMemoryTest() { loop_.Cleanup(); }

 private:
  debug_ipc::PlatformMessageLoop loop_;
};

class MyMockRegisterSet : public RegisterSet {
 public:
  void AddRegister(RegisterCategory::Type cat_type, RegisterID id,
                   uint32_t length, uint64_t value) {
    std::vector<uint8_t> data(sizeof(value));
    memcpy(&data[0], &value, sizeof(value));
    debug_ipc::Register ipc_reg({id, std::move(data)});
    cat_map_[cat_type].push_back(Register(std::move(ipc_reg)));
  }

  const CategoryMap& category_map() const override {
    return cat_map_;
  }

 private:
  CategoryMap cat_map_;
};

}  // namespace

TEST_F(AnalyzeMemoryTest, Basic) {
  Session session;
  MyMockProcess process(&session);

  constexpr uint64_t kBegin = 0x1000;
  constexpr uint32_t kLen = 24;  // 3 lines of output (8 bytes each).

  AnalyzeMemoryOptions opts;
  opts.process = &process;
  opts.begin_address = kBegin;
  opts.bytes_to_read = kLen;

  // The callback just saves the buffer to "output".
  OutputBuffer output;
  auto analysis = fxl::MakeRefCounted<MemoryAnalysis>(
      opts,
      [&output](const Err& err, OutputBuffer analysis, uint64_t next_addr) {
        output = analysis;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  // Setup address space. Make one region inside another. The innermost one
  // should be the one reported.
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

  // Setup frames.
  std::vector<Frame*> frames;
  const uint64_t kStack0SP = kBegin;
  const uint64_t kStack1SP = kBegin + 8;
  MockFrame frame0(nullptr, nullptr, debug_ipc::StackFrame{0x100, kStack0SP},
                   Location(Location::State::kSymbolized, 0x1234));
  MockFrame frame1(nullptr, nullptr, debug_ipc::StackFrame{0x108, kStack1SP},
                   Location(Location::State::kSymbolized, 0x1234));
  frames.push_back(&frame0);
  frames.push_back(&frame1);
  analysis->SetFrames(frames);

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

  // Setup registers (ESP points to beginning of block).
  constexpr uint64_t kAway = 0xFF00000000000;

  MyMockRegisterSet registers;
  registers.AddRegister(RegisterCategory::Type::kGeneral, RegisterID::kX64_rax, 8u,
                        kBegin);
  registers.AddRegister(RegisterCategory::Type::kGeneral, RegisterID::kX64_rcx, 8u,
                        kAway);
  analysis->SetRegisters(registers);

  analysis->Schedule(opts);
  debug_ipc::MessageLoop::Current()->Run();

  // The pointer to "inner" aspace entry should be annotated. The "outer"
  // aspace entry is too large and so will be omitted.
  EXPECT_EQ("Address               Data \n"
            " 0x1000 0x0000000000001000 ◁ rax. ▷ inside map \"inner\"\n"
            " 0x1008 0x0000000010000000 ◁ frame 1 SP\n"
            " 0x1010 0x0000000000000000 \n",
            output.AsString());
}

}  // namespace zxdb
