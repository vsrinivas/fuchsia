// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_over_thread_controller.h"
#include "garnet/bin/zxdb/client/inline_thread_controller_test.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/address_ranges.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/line_details.h"
#include "garnet/bin/zxdb/symbols/mock_module_symbols.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class StepOverThreadControllerTest : public InlineThreadControllerTest {};

// Tests a "step over" including a function call that's skipped. This generates
// an internal "finish" command to get out of the subroutine.
TEST_F(StepOverThreadControllerTest, InOutFinish) {
  // Step as long as we're in this range. Using the "code range" for stepping
  // allows us to avoid dependencies on the symbol subsystem.
  constexpr uint64_t kBeginAddr = 0x1000;
  constexpr uint64_t kEndAddr = 0x1010;

  // This is the frame we're starting stepping from.
  constexpr uint64_t kInitialBP = 0x2000;
  constexpr uint64_t kInitialSP = kInitialBP - 8;

  // The previous frame on the call stack.
  constexpr uint64_t kPrevSP = kInitialBP + 16;
  constexpr uint64_t kPrevBP = kPrevSP + 8;

  // Set up the thread to be stopped at the beginning of our range.
  debug_ipc::NotifyException exception;
  exception.process_koid = process()->GetKoid();
  exception.type = debug_ipc::NotifyException::Type::kSingleStep;
  exception.thread.koid = thread()->GetKoid();
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.thread.frames.resize(2);
  exception.thread.frames[0].ip = kBeginAddr;
  exception.thread.frames[0].sp = kInitialSP;
  exception.thread.frames[0].bp = kInitialBP;
  exception.thread.frames[1].ip = kBeginAddr - 0x100;
  exception.thread.frames[1].sp = kPrevSP;
  exception.thread.frames[1].bp = kPrevBP;
  InjectException(exception);

  // Continue the thread with the controller stepping in range.
  auto step_over = std::make_unique<StepOverThreadController>(
      AddressRanges(AddressRange(kBeginAddr, kEndAddr)));
  bool continued = false;
  thread()->ContinueWith(std::move(step_over), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });

  // It should have been able to step without doing any further async work.
  EXPECT_TRUE(continued);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Issue a stop in the range. This should get transparently resumed. In
  // general the backend won't issue this since it will continue stepping in
  // the given range, but it could, and we should resume anyway.
  exception.thread.frames[0].ip += 4;
  InjectException(exception);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Issue a stop in a new stack frame. The base pointer will be the same as
  // the outer function since the prologue hasn't executed yet. The previous
  // frame's IP will be the return address.
  constexpr uint64_t kInnerSP = kInitialSP - 8;
  exception.thread.frames.emplace(exception.thread.frames.begin());
  exception.thread.frames[0].ip = 0x3000;
  exception.thread.frames[0].sp = kInnerSP;
  exception.thread.frames[0].bp = kInitialSP;
  exception.thread.frames[1].ip += 4;
  InjectException(exception);

  // That should have sent a resume + a breakpoint set at the frame 1 IP (this
  // breakpoint is implementing the "finish" to step out of the function call).
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0, mock_remote_api()->breakpoint_remove_count());
  EXPECT_EQ(exception.thread.frames[1].ip,
            mock_remote_api()->last_breakpoint_address());

  // Send a breakpoint completion notification at the previous stack frame.
  exception.thread.frames.erase(
      exception.thread.frames.begin());  // Erase topmost.
  // Breakpoint exceptions are "software".
  exception.type = debug_ipc::NotifyException::Type::kSoftware;
  exception.hit_breakpoints.resize(1);
  exception.hit_breakpoints[0].breakpoint_id =
      mock_remote_api()->last_breakpoint_id();
  exception.hit_breakpoints[0].hit_count = 1;
  InjectException(exception);

  // That should have removed the breakpoint and resumed the thread.
  EXPECT_EQ(1, mock_remote_api()->breakpoint_remove_count());
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Last exception is outside the range (the end is non-inclusive).
  exception.hit_breakpoints.clear();
  exception.thread.frames[0].ip = kEndAddr;
  InjectException(exception);

  // Should have stopped.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped.
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kBlocked, thread()->GetState());
}

// Tests "step over" stepping from before an inline function to the call of
// the inline function. This is tricky because that call is actually the
// first instruction of the inline function so needs special handling. The
// code being tested would look like this:
//
//   void Top() {
//     foo();
// >   NonInlinedFunction(TopInlineFunction(), SecondInlineFunction());
//     bar();
//   }
//
// Since we're testing "step over", the location after the step should be on
// the next line:
//
//   void Top() {
//     foo();
//     NonInlinedFunction(TopInlineFunction(), SecondInlineFunction());
// >   bar();
//   }
//
// To do this, it steps into and out of TopInlineFunction(), then into and out
// of SecondInlineFunction(), then into and out of NonInlinedFunction().
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
  // Add line information required for the stepping. The first instruction of
  // the inlined function is two places:
  //   stack[0] = first instruction of inline @ kTopInlineFileLine.
  //   stack[1] = first instruction of inline @ kTopFileLine
  auto mock_frames = GetStack();
  FileLine step_line = kTopFileLine;  // Line being stepped over.

  // The line table holds the mapping for the inlined code
  // (kTopInlineFileLine) at the ambiguous address so that's what we add here.
  // The stepper should handle the fact that stack[1]'s file_line is different
  // but at the same address.
  module_symbols()->AddLineDetails(
      kTopInlineFunctionRange.begin(),
      LineDetails(kTopInlineFileLine,
                  {LineDetails::LineEntry(kTopInlineFunctionRange)}));

  // The SecondInlineFunction() immediately following the first.
  FileLine second_inline_line("random.cc", 3746);
  AddressRange second_inline_range(kTopInlineFunctionRange.end(),
                                   kTopInlineFunctionRange.end() + 4);
  module_symbols()->AddLineDetails(
      second_inline_range.begin(),
      LineDetails(second_inline_line,
                  {LineDetails::LineEntry(second_inline_range)}));

  // Line information for the address following the inlined function but on
  // the same line (this is the code for the NonInlinedFunction() call).
  const uint64_t kNonInlinedAddress = second_inline_range.end();
  AddressRange non_inlined_call_range(kNonInlinedAddress, kNonInlinedAddress + 4);
  module_symbols()->AddLineDetails(
      kNonInlinedAddress,
      LineDetails(step_line,
                  {LineDetails::LineEntry(non_inlined_call_range)}));

  // Code for the line after (the "bar()" call in the example). This maps to
  // a different line (immediately following) which is how we know to stop.
  const uint64_t kFollowingAddress = non_inlined_call_range.end();
  AddressRange following_range(kFollowingAddress, kFollowingAddress + 4);
  FileLine following_line(kTopFileLine.file(), kTopFileLine.line() + 1);
  module_symbols()->AddLineDetails(
      kFollowingAddress,
      LineDetails(following_line, {LineDetails::LineEntry(following_range)}));

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // -----------------------------------------------------------------------------
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
  thread()->ContinueWith(
      std::make_unique<StepOverThreadController>(StepMode::kSourceLine),
      [](const Err& err) {});

  // That should have requested a synthetic exception which will be sent out
  // asynchronously. The Resume() call will cause the MockRemoteAPI to exit the
  // message loop.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Nothing yet.
  loop().PostTask(FROM_HERE, [loop = &loop()](){ loop->QuitNow(); });
  loop().Run();

  // The synthetic exception will trigger the step over controller to exit
  // the inline frame. It will single step the CPU to get out of the inline
  // function so the thread should be resumed now.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continued.

  // Issue an exception in the middle of the inline function. Since we're
  // stepping over it, the controller should continue.
  //
  // Current code is at position (2) in the diagram above. Stack:
  //   TopInline
  //   Top
  //   ...
  mock_frames = GetStack();
  mock_frames[0]->SetAddress(kTopInlineFunctionRange.begin() + 1);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Make the 2nd inline function.
  auto second_inline_func =
      fxl::MakeRefCounted<Function>(Symbol::kTagInlinedSubroutine);
  second_inline_func->set_assigned_name("SecondInlineFunction");
  second_inline_func->set_code_ranges(AddressRanges(second_inline_range));
  Location second_inline_loc(
      second_inline_range.begin(), second_inline_line, 0,
      SymbolContext::ForRelativeAddresses(), LazySymbol(second_inline_func));

  // The code exits the first inline function and is now at the first
  // instruction of the second inline function. This is an ambiguous location.
  //
  // Sets to position (3) in the diagram above. Stack:
  //   SecondInline (ambiguous address @ beginning of inline block)
  //   Top
  mock_frames = GetStack();
  mock_frames[0] =
      std::make_unique<MockFrame>(
          nullptr, nullptr,
          debug_ipc::StackFrame(second_inline_range.begin(), kTopSP, kTopSP),
          second_inline_loc, mock_frames[1].get(), true);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  // That should have hidden the top ambiguous inline frame, the StepOver
  // controller should have decided to keep going since it's still on the same
  // line, and then the step controller should have unhidden the top frame to
  // step into the inline function.

  // As of this writing, the "step over" controller delegates to the step
  // controller which steps into the inline routine. This skips the "Continue"
  // call on the thread since we're already in the middle of stepping and is
  // not asynchronous (unlike when we do a "step into" at the beginning of a
  // step operation). This is an implementation detail, however, and may
  // change, so this test code doesn't make assumptions about asynchronous or
  // not for this step.
  loop().PostTask(FROM_HERE, [loop = &loop()](){ loop->QuitNow(); });
  loop().Run();
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // Issue a step after the 2nd inline function. But this still has the
  // same line as the callers for both the inlines, so it should continue.
  //
  // Sets to position (4) in the diagram above. Stack:
  //   Top (same line we were on before)
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Remove inline we finished.
  mock_frames[0]->SetAddress(kNonInlinedAddress);
  mock_frames[0]->SetFileLine(step_line);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
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
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stop.
}

}  // namespace zxdb
