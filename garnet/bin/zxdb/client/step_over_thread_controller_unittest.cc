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
  EXPECT_EQ(1, mock_remote_api()->resume_count());

  // Issue a stop in the range. This should get transparently resumed. In
  // general the backend won't issue this since it will continue stepping in
  // the given range, but it could, and we should resume anyway.
  exception.thread.frames[0].ip += 4;
  InjectException(exception);
  EXPECT_EQ(2, mock_remote_api()->resume_count());

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
  EXPECT_EQ(3, mock_remote_api()->resume_count());
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
  EXPECT_EQ(4, mock_remote_api()->resume_count());

  // Last exception is outside the range (the end is non-inclusive).
  exception.hit_breakpoints.clear();
  exception.thread.frames[0].ip = kEndAddr;
  InjectException(exception);

  // Should have stopped.
  EXPECT_EQ(4, mock_remote_api()->resume_count());  // Same value as above.
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kBlocked, thread()->GetState());
}

// Tests "step over" stepping from before an inline function to the call of
// the inline function. This is tricky because that call is actually the
// first instruction of the inline function so needs special handling.
TEST_F(StepOverThreadControllerTest, Inline) {
  // The GetStack() function returns a stack in an inline function. Modify
  // the initial state so it's right before the inline call (popping the
  // inline frame off, exposing the physical frame right before the call).
  auto mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());
  ASSERT_FALSE(mock_frames[0]->IsInline());

  // Step in a range right before the inline call.
  AddressRange step_range1(mock_frames[0]->GetAddress() - 4,
                           mock_frames[0]->GetAddress());
  mock_frames[0]->SetAddress(step_range1.begin());

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  thread()->ContinueWith(
      std::make_unique<StepOverThreadController>(AddressRanges(step_range1)),
      [](const Err& err) {});
  EXPECT_EQ(1, mock_remote_api()->resume_count());  // Continued.

  // Issue a stop at the beginning of the inline function. We provide the
  // full stack (including the inline function) because that's where the
  // address resolves to.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(GetStack()), true);
  EXPECT_EQ(1, mock_remote_api()->resume_count());  // Unchanged (stopped).

  // The "step over" controller should have reported "stop" and fixed up the
  // stack to be at the call point of the inline function.
  EXPECT_EQ(1u, thread()->GetStack().hide_top_inline_frame_count());

  // Now step over the inline call.
  thread()->ContinueWith(std::make_unique<StepOverThreadController>(
                             AddressRanges(kTopFunctionRange)),
                         [](const Err& err) {});
  EXPECT_EQ(2, mock_remote_api()->resume_count());  // Continued.

  // Pretend that immediately following that inline call is another inline
  // call. This is as if two inline functions are back-to-back in the source
  // code, so "returning" from the first immediately pops you in the first
  // instruction of the next.

  // The function range for inline function immediately following the first.
  AddressRange second_inline_range(kTopFunctionRange.end(),
                                   kTopFunctionRange.end() + 4);
  auto second_inline_func =
      fxl::MakeRefCounted<Function>(Symbol::kTagInlinedSubroutine);
  second_inline_func->set_assigned_name("Second");
  second_inline_func->set_code_ranges(AddressRanges(second_inline_range));

  Location second_inline_loc(
      second_inline_range.begin(), FileLine("file.cc", 21), 0,
      SymbolContext::ForRelativeAddresses(), LazySymbol(second_inline_func));

  // Clear so we can tell in the next step that it was actually changed.
  thread()->GetStack().SetHideTopInlineFrameCount(0);

  // Replace the inline function at the top with our new one.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Erase old top inline function.
  mock_frames.insert(
      mock_frames.begin(),
      std::make_unique<MockFrame>(
          nullptr, nullptr,
          debug_ipc::StackFrame(second_inline_range.begin(), kTopSP, kTopSP),
          second_inline_loc, mock_frames[0].get()));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // The step controller should have fixed the stack to look like the call
  // to the second inline.
  EXPECT_EQ(1u, thread()->GetStack().hide_top_inline_frame_count());
}

}  // namespace zxdb
