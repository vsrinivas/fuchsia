// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_finish.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"

namespace zxdb {

namespace {

using VerbFinishTest = ConsoleTest;

}  // namespace

// This is an integration test covering "finish" plus printing the function return information.
TEST_F(VerbFinishTest, ReturnValue) {
  // Make the called function. It returns a uint64_t*. We don't actually need to define the calling
  // function symbol since it's never used.
  auto uint64_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t");
  auto uint64_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, uint64_type);
  auto called_function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  called_function->set_assigned_name("MyFunction");
  called_function->set_return_type(uint64_ptr_type);

  SymbolContext symbol_context(0x8000000);
  constexpr uint64_t kCalledIP = 0x8000000;
  constexpr uint64_t kReturnIP = 0x8000010;
  constexpr uint64_t kCalledSP = 0x2000000;
  constexpr uint64_t kReturnSP = 0x2000008;

  // Indicate a stop at the end of the called function.
  std::vector<std::unique_ptr<Frame>> frames;
  Location location(kCalledIP, FileLine(), 0, symbol_context, called_function);
  Location return_location(kReturnIP, FileLine(), 0, symbol_context);
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, kCalledSP));
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), return_location, kReturnSP));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  // That will produce some output we don't care about.
  loop().RunUntilNoTasks();
  console().FlushOutputEvents();

  // The address the function returns and the data it points to.
  constexpr uint64_t kReturnValuePtr = 0x67200000;
  const std::vector<uint8_t> kPointedToData{42, 0, 0, 0, 0, 0, 0, 0};  // 42 little-endian.

  // The returned pointer is set in rax. Make a frame and populate that.
  EXPECT_EQ(debug::Arch::kX64, GetArch());
  auto return_frame = std::make_unique<MockFrame>(&session(), thread(), return_location, kReturnSP);
  MockSymbolDataProvider* provider = return_frame->GetMockSymbolDataProvider();
  provider->AddRegisterValue(debug_ipc::RegisterID::kX64_rax, true, kReturnValuePtr);
  provider->AddMemory(kReturnValuePtr, kPointedToData);

  // Tell the debugger to finish this frame.
  console().ProcessInputLine("finish");
  console().FlushOutputEvents();

  // Indicate a stop at the call site. This is the breakpoint that the "finish" controller will set.
  std::vector<debug_ipc::BreakpointStats> hit_breakpoints;
  hit_breakpoints.emplace_back();
  hit_breakpoints[0].id = mock_remote_api()->last_breakpoint_id();
  frames.push_back(std::move(return_frame));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSoftwareBreakpoint, std::move(frames), true,
                           hit_breakpoints);

  // The system should evaluate the return value, print it, and then report the stop.
  loop().RunUntilNoTasks();

  // The return value should be decoded, the pointer should be resolved.
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("MyFunction 🡲 (*)0x67200000 ➔ 42", event.output.AsString());

  // After the return value should be the stop information (we didn't provide any calling symbols).
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("🛑 0x8000010 (no symbol info)\n", event.output.AsString());
}

}  // namespace zxdb
