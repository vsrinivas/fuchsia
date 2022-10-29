// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/function_thread_controller.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"

namespace zxdb {

namespace {

class FunctionThreadControllerTest : public InlineThreadControllerTest {
 public:
  void SetUnsymbolizedSetting(bool stop_on_no_symbols) {
    thread()->session()->system().settings().SetBool(ClientSettings::System::kSkipUnsymbolized,
                                                     !stop_on_no_symbols);
  }

  // Backend that runs a test for stepping into an unsymbolized function, both for when we want it
  // to stop (param = true) and continue (param = false).
  void DoUnsymbolizedPltCallTest(bool stop_on_no_symbols);
};

}  // namespace

// This also tests the StepThroughPltThreadController and both of their integration with the
// StepThreadController. Both of these sub-controllers are used by the "step into" controller.
void FunctionThreadControllerTest::DoUnsymbolizedPltCallTest(bool stop_on_no_symbols) {
  SymbolContext sym_context(kSymbolizedModuleAddress);

  // Jump from src to dest and return, then to kOutOfRange.
  const uint64_t kAddrSrc = kSymbolizedModuleAddress + 0x100;
  const uint64_t kAddrDest = kUnsymbolizedModuleAddress + 0x200;
  const uint64_t kAddrReturn = kAddrSrc + 4;
  const uint64_t kAddrOutOfRange = kAddrReturn + 4;

  const uint64_t kSrcSP = 0x5000;
  const uint64_t kDestSP = 0x4ff0;

  auto src_sym = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);

  // The line where the step starts from.
  FileLine src_line("/path/src.cc", 1);
  LineDetails src_details(src_line);
  src_details.entries().push_back({0, AddressRange(kAddrSrc, kAddrOutOfRange)});
  module_symbols()->AddLineDetails(kAddrSrc, src_details);

  // The line after the step operation.
  FileLine out_of_range_line("/path/src.cc", 2);
  LineDetails out_of_range_details(out_of_range_line);
  out_of_range_details.entries().push_back({0, AddressRange(kAddrOutOfRange, kAddrOutOfRange + 1)});
  module_symbols()->AddLineDetails(kAddrOutOfRange, out_of_range_details);

  // PLT symbol info. This thunk is in the symbolized module to call into the unsymbolized one.
  const uint64_t kAddrPltRelative = 0x5980;
  const uint64_t kAddrPltAbsolute = kAddrPltRelative + kSymbolizedModuleAddress;
  const std::string kPltName = "plt_call";
  ElfSymbolRecord plt_record(ElfSymbolType::kPlt, kAddrPltRelative, 1, kPltName);
  auto plt_symbol = fxl::MakeRefCounted<ElfSymbol>(module_symbols()->GetWeakPtr(), plt_record);
  Location plt_loc(kAddrPltAbsolute, FileLine(), 0, sym_context, plt_symbol);
  Identifier plt_identifier(IdentifierComponent(SpecialIdentifier::kPlt, kPltName));
  module_symbols()->AddSymbolLocations(plt_identifier, {plt_loc});

  // Other locations for each step below.
  Location source_loc(kAddrSrc, src_line, 0, sym_context, src_sym);
  Location dest_loc(kAddrDest, FileLine("foo.cc", 1), 0, sym_context);
  Location return_loc(kAddrReturn, src_line, 0, sym_context, src_sym);
  Location out_of_range_loc(kAddrOutOfRange, out_of_range_line, 0, sym_context, src_sym);

  // Destination of the PLT call. This is an ELF symbol (not a PLT one which is for the trampoline).
  // The "until" controller will look up this symbol to set a breakpoint on the destination.
  Identifier plt_dest_identifier(IdentifierComponent(SpecialIdentifier::kElf, kPltName));
  unsymbolized_module_symbols()->AddSymbolLocations(plt_dest_identifier, {dest_loc});

  // Set up the thread to be stopped at the beginning of our range.
  std::vector<std::unique_ptr<Frame>> stack;
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), source_loc, kSrcSP, kSrcSP));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep, std::move(stack), true);

  // Continue the thread with the controller stepping in range.
  auto step_into = std::make_unique<StepThreadController>(StepMode::kSourceLine);
  SetUnsymbolizedSetting(stop_on_no_symbols);
  bool continued = false;
  thread()->ContinueWith(std::move(step_into), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });

  // It should have been able to step without doing any further async work.
  EXPECT_TRUE(continued);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop on the PLT call. The PLT controller should continue it.
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), plt_loc, kDestSP, kDestSP));
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), return_loc, kSrcSP, kSrcSP));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep, std::move(stack), true);
  EXPECT_TRUE(continued);

  // The PLT controller initializes asynchronously after the breakpoint is confirmed set. In real
  // life this will be woken up by the debug_agent's set breakpoint reply, but our mock breakpoints
  // just post a task to respond.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  loop().RunUntilNoTasks();
  // That should wake up the "until" controller which should then tell the PLT controller which will
  // then request a continue.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // The PLT controller will have created an "until" controller which should set a breakpoint at the
  // destination of the call.
  EXPECT_EQ(mock_remote_api()->last_breakpoint_address(), kAddrDest);
  debug_ipc::BreakpointStats breakpoint_hit{
      .id = static_cast<uint32_t>(mock_remote_api()->last_breakpoint_id()), .hit_count = 1};
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), dest_loc, kDestSP, kDestSP));
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), return_loc, kSrcSP, kSrcSP));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSoftwareBreakpoint, std::move(stack), true,
                           {breakpoint_hit});
  if (stop_on_no_symbols) {
    // For this variant of the test, the unsymbolized thunk should have stopped stepping.
    EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped
    EXPECT_EQ(std::make_optional(debug_ipc::ThreadRecord::State::kBlocked), thread()->GetState());
    return;
  }

  // The rest of this test is the "step over unsymbolized thunks" case. It should have automatically
  // resumed from the previous exception.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Send a breakpoint completion notification at the previous stack frame. Breakpoint exceptions
  // are "software". We also have to send the hit breakpoint ID.
  stack.push_back(std::make_unique<MockFrame>(&session(), thread(), return_loc, kSrcSP, kSrcSP));
  debug_ipc::BreakpointStats breakpoint{
      .id = static_cast<uint32_t>(mock_remote_api()->last_breakpoint_id()), .hit_count = 1};
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSoftwareBreakpoint, std::move(stack), true,
                           {breakpoint});

  // This should have continued since the return address is still in the original address range.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop on dest instruction, this is still in range so we should continue.
  stack.push_back(
      std::make_unique<MockFrame>(&session(), thread(), out_of_range_loc, kSrcSP, kSrcSP));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep, std::move(stack), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped
  EXPECT_EQ(std::make_optional(debug_ipc::ThreadRecord::State::kBlocked), thread()->GetState());
}

TEST_F(FunctionThreadControllerTest, UnsymbolizedPltCallStepOver) {
  DoUnsymbolizedPltCallTest(false);
}

TEST_F(FunctionThreadControllerTest, UnsymbolizedPltCallStepInto) {
  DoUnsymbolizedPltCallTest(true);
}

}  // namespace zxdb
