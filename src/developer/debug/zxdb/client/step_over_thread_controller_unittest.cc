// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/address_ranges.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

class StepOverThreadControllerTest : public InlineThreadControllerTest {};

// Tests "step over" stepping from before an inline function to the call of the inline function.
// This is tricky because that call is actually the first instruction of the inline function so
// needs special handling. The code being tested would look like this:
//
//   void Top() {
//     foo();
// >   NonInlinedFunction(TopInlineFunction(), SecondInlineFunction());
//     bar();
//   }
//
// Since we're testing "step over", the location after the step should be on the next line:
//
//   void Top() {
//     foo();
//     NonInlinedFunction(TopInlineFunction(), SecondInlineFunction());
// >   bar();
//   }
//
// To do this, it steps into and out of TopInlineFunction(), then into and out of
// SecondInlineFunction(), then into and out of NonInlinedFunction().
//
// Code layout:
//
//   +-----------------------------------------------------+
//   | Top()                                               |
//   |       <code for foo() call>                         |
//   |       +------------------------------------------+  |
//   |       | Inlined code for TopInlineFunction()     |  |  <- (1)
//   |       |                                          |  |  <- (2)
//   |       +------------------------------------------+  |
//   |       | Inlined code for SecondInlineFunction()  |  |  <- (3)
//   |       |                                          |  |
//   |       +------------------------------------------+  |
//   |       <code for NonInlinedFunction() call>          |  <- (4)
//   |       <code for bar() call>                         |  <- (5)
//   |                                                     |
//   +-----------------------------------------------------+
TEST_F(StepOverThreadControllerTest, Inline) {
  // Add line information required for the stepping. The first instruction of the inlined function
  // is two places:
  //   stack[0] = first instruction of inline @ kTopInlineFileLine.
  //   stack[1] = first instruction of inline @ kTopFileLine
  auto mock_frames = GetStack();
  FileLine step_line = kTopFileLine;  // Line being stepped over.

  // The line table holds the mapping for the inlined code (kTopInlineFileLine) at the ambiguous
  // address so that's what we add here.  The stepper should handle the fact that stack[1]'s
  // file_line is different but at the same address.
  module_symbols()->AddLineDetails(
      kTopInlineFunctionRange.begin(),
      LineDetails(kTopInlineFileLine, {LineDetails::LineEntry(kTopInlineFunctionRange)}));

  // The SecondInlineFunction() immediately following the first.
  FileLine second_inline_line("random.cc", 3746);
  AddressRange second_inline_range(kTopInlineFunctionRange.end(),
                                   kTopInlineFunctionRange.end() + 4);
  module_symbols()->AddLineDetails(
      second_inline_range.begin(),
      LineDetails(second_inline_line, {LineDetails::LineEntry(second_inline_range)}));

  // Line information for the address following the inlined function but on the same line (this is
  // the code for the NonInlinedFunction() call).
  const uint64_t kNonInlinedAddress = second_inline_range.end();
  AddressRange non_inlined_call_range(kNonInlinedAddress, kNonInlinedAddress + 4);
  module_symbols()->AddLineDetails(
      kNonInlinedAddress, LineDetails(step_line, {LineDetails::LineEntry(non_inlined_call_range)}));

  // Code for the line after (the "bar()" call in the example). This maps to a different line
  // (immediately following) which is how we know to stop.
  const uint64_t kFollowingAddress = non_inlined_call_range.end();
  AddressRange following_range(kFollowingAddress, kFollowingAddress + 4);
  FileLine following_line(kTopFileLine.file(), kTopFileLine.line() + 1);
  module_symbols()->AddLineDetails(
      kFollowingAddress, LineDetails(following_line, {LineDetails::LineEntry(following_range)}));

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // -----------------------------------------------------------------------------------------------
  // Done with setup, actual test following.
  //
  // Current stack is:
  //   TopInline
  //   Top
  //   ...

  Stack& stack = thread()->GetStack();

  // The first instruction of the inlined function should be ambiguous.
  ASSERT_EQ(1u, stack.GetAmbiguousInlineFrameCount());

  // Hide the inline frame because we want to step over the inlined function.
  stack.SetHideAmbiguousInlineFrameCount(1);

  // Start to step over the top stack frame's line.
  //
  // Current code is at position (1) in the diagram above. Stack:
  //   [hidden w/ ambiguous address: TopInline]
  //   Top
  //   ...
  EXPECT_EQ(step_line, stack[0]->GetLocation().file_line());
  thread()->ContinueWith(std::make_unique<StepOverThreadController>(StepMode::kSourceLine),
                         [](const Err& err) {});

  // That should have requested a synthetic exception which will be sent out asynchronously. The
  // Resume() call will cause the MockRemoteAPI to exit the message loop.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Nothing yet.
  loop().PostTask(FROM_HERE, [loop = &loop()]() { loop->QuitNow(); });
  loop().Run();

  // The synthetic exception will trigger the step over controller to exit the inline frame. It will
  // single step the CPU to get out of the inline function so the thread should be resumed now.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continued.

  // Issue an exception in the middle of the inline function. Since we're stepping over it, the
  // controller should continue.
  //
  // Current code is at position (2) in the diagram above. Stack:
  //   TopInline
  //   Top
  //   ...
  mock_frames = GetStack();
  mock_frames[0]->SetAddress(kTopInlineFunctionRange.begin() + 1);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Make the 2nd inline function.
  auto second_inline_func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  second_inline_func->set_assigned_name("SecondInlineFunction");
  second_inline_func->set_code_ranges(AddressRanges(second_inline_range));
  Location second_inline_loc(second_inline_range.begin(), second_inline_line, 0,
                             SymbolContext::ForRelativeAddresses(), second_inline_func);

  // The code exits the first inline function and is now at the first instruction of the second
  // inline function. This is an ambiguous location.
  //
  // Sets to position (3) in the diagram above. Stack:
  //   SecondInline (ambiguous address @ beginning of inline block)
  //   Top
  mock_frames = GetStack();
  mock_frames[0] = std::make_unique<MockFrame>(nullptr, nullptr, second_inline_loc, kTopSP, 0,
                                               std::vector<debug_ipc::Register>(), kTopSP,
                                               mock_frames[1].get(), true);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  // That should have hidden the top ambiguous inline frame, the StepOver controller should have
  // decided to keep going since it's still on the same line, and then the step controller should
  // have unhidden the top frame to step into the inline function.

  // As of this writing, the "step over" controller delegates to the step controller which steps
  // into the inline routine. This skips the "Continue" call on the thread since we're already in
  // the middle of stepping and is not asynchronous (unlike when we do a "step into" at the
  // beginning of a step operation). This is an implementation detail, however, and may change, so
  // this test code doesn't make assumptions about asynchronous or not for this step.
  loop().PostTask(FROM_HERE, [loop = &loop()]() { loop->QuitNow(); });
  loop().Run();
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // Issue a step after the 2nd inline function. But this still has the same line as the callers for
  // both the inlines, so it should continue.
  //
  // Sets to position (4) in the diagram above. Stack:
  //   Top (same line we were on before)
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Remove inline we finished.
  mock_frames[0]->SetAddress(kNonInlinedAddress);
  mock_frames[0]->SetFileLine(step_line);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Issue a step for a different line, this should finally stop.
  //
  // Sets to position (5) in the diagram above. Stack:
  //   Top (different line)
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Remove inline we finished.
  mock_frames[0]->SetAddress(kFollowingAddress);
  mock_frames[0]->SetFileLine(following_line);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stop.
}

// The line table can contain entries with "line 0" that correspond to compiler-generated code.
// These should be transparently stepped over as if they're part of the original line being stepped.
// Most of the logic around "0 lines" is handled by the StepThreadController.
//
// This test covers the case where where it steps over a call, and the return address of that call
// maps to one of these 0 lines. Execution should resume from that point as if it was part of the
// original line being stepped.
TEST_F(StepOverThreadControllerTest, OutToZeroLine) {
  // The location we're stepping from is the middle frame.
  const uint64_t kFromAddress = kMiddleFunctionRange.begin();
  FileLine step_line = kMiddleFileLine;  // Line being stepped over.

  const uint64_t kBottomAddress = 0x1000;
  std::vector<std::unique_ptr<MockFrame>> mock_frames;
  mock_frames.push_back(GetMiddleFrame(kFromAddress));
  mock_frames.push_back(GetBottomFrame(kBottomAddress));

  // Source line table information. This is a one-byte range for the instruction where the "step
  // over" beings.
  module_symbols()->AddLineDetails(
      kFromAddress,
      LineDetails(kMiddleFileLine,
                  {LineDetails::LineEntry(AddressRange(kFromAddress, kFromAddress + 1))}));

  // Line info for the top function call.
  const uint64_t kTopAddress = kTopFunctionRange.begin();
  module_symbols()->AddLineDetails(
      kTopAddress, LineDetails(kTopFileLine, {LineDetails::LineEntry(kTopFunctionRange)}));

  // The function call returns to the next instruction which gives a "0" line number. Note that the
  // file name is still present because this is how DWARF usually encodes things.
  const uint64_t kReturnAddress = kFromAddress + 1;
  const FileLine kZeroFileLine(kMiddleFileLine.file(), 0);
  module_symbols()->AddLineDetails(
      kReturnAddress,
      LineDetails(kZeroFileLine,
                  {LineDetails::LineEntry(AddressRange(kReturnAddress, kReturnAddress + 1))}));

  // The third byte is a new line number. This is where stepping should stop.
  const uint64_t kFinalAddress = kReturnAddress + 1;
  const FileLine kFinalFileLine(kMiddleFileLine.file(), kMiddleFileLine.line() + 1);
  module_symbols()->AddLineDetails(
      kFinalAddress,
      LineDetails(kFinalFileLine,
                  {LineDetails::LineEntry(AddressRange(kFinalAddress, kFinalAddress + 1))}));

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // -----------------------------------------------------------------------------------------------
  // Done with setup, actual test following.
  //
  // Current stack is:
  //   Middle  (top of stack)
  //   Bottom

  // Step over the "from" address.
  thread()->ContinueWith(std::make_unique<StepOverThreadController>(StepMode::kSourceLine),
                         [](const Err& err) {});
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Stop in a new stack frame called by the previous execution. It should continue.
  mock_frames.push_back(GetTopFrame(kTopAddress));
  mock_frames.push_back(GetMiddleFrame(kFromAddress));
  mock_frames.push_back(GetBottomFrame(kBottomAddress));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Execution returns to the original frame at the next instruction. This is the instruction with
  // the "line 0" annotation and it should be resumed.  We can't use GetMiddleFrame() here because
  // we need to supply a specific FileLine.
  mock_frames.push_back(std::make_unique<MockFrame>(
      nullptr, nullptr,
      Location(kReturnAddress, kZeroFileLine, 0, SymbolContext::ForRelativeAddresses(),
               GetMiddleFunction()),
      kMiddleSP, kBottomSP, std::vector<debug_ipc::Register>(), kMiddleSP));
  mock_frames.push_back(GetBottomFrame(kBottomAddress));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // The next instruction is on a different line, reporting a stop there should finish stepping.
  mock_frames.push_back(std::make_unique<MockFrame>(
      nullptr, nullptr,
      Location(kFinalAddress, kFinalFileLine, 0, SymbolContext::ForRelativeAddresses(),
               GetMiddleFunction()),
      kMiddleSP, kBottomSP, std::vector<debug_ipc::Register>(), kMiddleSP));
  mock_frames.push_back(GetBottomFrame(kBottomAddress));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stop.
}

}  // namespace zxdb
