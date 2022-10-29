// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"

namespace zxdb {

namespace {

class FinishThreadControllerTest : public InlineThreadControllerTest {};

}  // namespace

// See also the FinishPhysicalFrameThreadController tests.

// Tests finishing a single inline frame. This finishes the top frame of the stack which is an
// inline function (see InlineThreadControllerTest for what the returned stack layout is).
TEST_F(FinishThreadControllerTest, FinishInline) {
  auto mock_frames = GetStack();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Since this never steps over a non-inline frame, the function return callback should never
  // be called.
  bool function_completion_called = false;

  // "Finish" from the top stack frame, which is an inline one.
  auto finish_controller = std::make_unique<FinishThreadController>(
      thread()->GetStack(), 0, [&function_completion_called](const FunctionReturnInfo&) {
        function_completion_called = true;
      });
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });

  // It should have been able to step without doing any further async work.
  EXPECT_TRUE(continued);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Do one step inside the inline function (add 4 to the address).
  mock_frames = GetStack();
  mock_frames[0]->SetAddress(mock_frames[0]->GetAddress() + 4);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // That's still inside the frame's range, so it should continue.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Set exception at the first instruction after the inline frame.
  mock_frames = GetStack();
  uint64_t after_inline = kTopInlineFunctionRange.end();
  mock_frames.erase(mock_frames.begin());  // Remove the inline function.
  mock_frames[0]->SetAddress(after_inline);

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Should not have resumed.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(std::make_optional(debug_ipc::ThreadRecord::State::kBlocked), thread()->GetState());

  // None of the above stepping should have triggered a non-inline function return.
  EXPECT_FALSE(function_completion_called);
}

// Finishes multiple frames, consisting of one physical frame finish followed by two inline frame
// finishes. This finishes to frame 4 (see InlineThreadControllerTest) which is the "middle"
// physical frame. It requires doing a "finish" of the top physical frame, then stepping through
// both middle inline frames.
TEST_F(FinishThreadControllerTest, FinishPhysicalAndInline) {
  auto mock_frames = GetStack();
  uint64_t frame_2_ip = mock_frames[2]->GetAddress();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Holds the result of any seen non-inline function returns.
  std::optional<FunctionReturnInfo> return_info;

  // "Finish" frame 3,
  auto finish_controller = std::make_unique<FinishThreadController>(
      thread()->GetStack(), 3,
      [&return_info](const FunctionReturnInfo& info) { return_info = info; });
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });

  // That should have sent a resume + a breakpoint set at the frame 2 IP (this breakpoint is
  // implementing the "finish" to step out of the frame 1 physical frame).
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0, mock_remote_api()->breakpoint_remove_count());
  EXPECT_EQ(frame_2_ip, mock_remote_api()->last_breakpoint_address());

  // Simulate a breakpoint hit of that breakpoint (breakpoint exceptions are "software").
  debug_ipc::NotifyException exception;
  exception.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  exception.thread.id.process = process()->GetKoid();
  exception.thread.id.thread = thread()->GetKoid();
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.hit_breakpoints.emplace_back();
  exception.hit_breakpoints[0].id = mock_remote_api()->last_breakpoint_id();

  // Create a stack now showing frame 2 as the top (new frame 0).
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 3);
  InjectExceptionWithStack(exception, MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // That should have triggered the function return call indicating the top function returned.
  ASSERT_TRUE(return_info);
  EXPECT_EQ(thread(), return_info->thread);
  EXPECT_EQ(GetTopFunction()->GetAssignedName(), return_info->symbol.Get()->GetAssignedName());

  // The breakpoint should have been cleared and the thread should have been resumed.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(1, mock_remote_api()->breakpoint_remove_count());

  // Do another stop 4 bytes later in the inline frame 2 which should get continued.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 3);
  mock_frames[0]->SetAddress(mock_frames[0]->GetAddress() + 4);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop in inline frame 1. This leaves inline frame 2 (right after its address range) but should
  // still continue since we haven't reached the target.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 4);
  mock_frames[0]->SetAddress(kMiddleInline2FunctionRange.end());
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop in middle frame which is the target (right after the inline 1 range).
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 5);
  mock_frames[0]->SetAddress(kMiddleInline1FunctionRange.end());
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped.
}

// This sets up a situation where the finish controller creates a "step over" controller in response
// to a breakpoint hit exception. The step over controller should not see the breakpoint hit and
// should continue as if it was not created from within a breakpoint hit.
//
// The situation where this can happen is:
//
//   FinishThreadController (FINISH#1) creates a new StepOverThreadController (OVER#1).
//     OVER finds a physical function call and
//       Creates a FinishThreadController (FINISH#2) to get out of it.
//       FINISH#2 creates a FinishPhysicalFrameThreadController (PHYSICAL) to get out of it.
//   The breakpoint for PHYSICAL is hit.
//     FINISH#2 completes.
//     OVER#1 completes.
//       FINISH#1 notices a new inline subframe immediately following the first.
//       FINISH#1 creates a new StepOverThreadController (OVER#2)
TEST_F(FinishThreadControllerTest, FinishPhysicalAndInline2) {
  // Stack:
  //   [0] MiddleInline2  <- OVER#1
  //   [1] MiddleInline1  <- finishing this one.
  //   [2] Middle
  //   [3] Bottom
  auto stack = GetStack();
  stack.erase(stack.begin(), stack.begin() + 2);  // Drop the "top" frames from the mock input.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(stack)), true);

  // Create FINISH#1 from above. This should notice we're in an inline frame, create OVER#1, and
  // continue.
  auto finish_controller = std::make_unique<FinishThreadController>(thread()->GetStack(), 0);
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0, mock_remote_api()->breakpoint_remove_count());

  // Simulate a physical frame call.
  //
  // Stack:
  //   [0] Top            <- PHYSICAL
  //   [1] MiddleInline2  <- OVER#1
  //   [2] MiddleInline1  <- finishing this one.
  //   [3] Middle
  //   [4] Bottom
  stack = GetStack();
  stack.erase(stack.begin(), stack.begin() + 1);  // Drop the top inline frame from the mock input.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(stack)), true);
  // That should have created PHYSICAL which will set a breakpoint on the return addr.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(1, mock_remote_api()->breakpoint_add_count());

  // Simulate a return from the physical frame call to a new inline frame
  //
  // Stack:
  //   [1] MiddleInline2.2  <- OVER#2
  //   [2] MiddleInline1    <- finishing this one.
  //   [3] Middle
  //   [4] Bottom
  stack = GetStack();
  stack.erase(stack.begin(), stack.begin() + 2);  // Drop both "top" frames.

  // Fix up the location so the MiddleInlin2 becomes MiddleInline2.2, a different inline function
  // immediately following it.
  const AddressRange middle_2_2_range(kMiddleInline2FunctionRange.end(),
                                      kMiddleInline2FunctionRange.end() + 2);
  auto middle2_2_func = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  middle2_2_func->set_assigned_name("MiddleInline2.2");
  middle2_2_func->set_code_ranges(AddressRanges(middle_2_2_range));
  stack[0]->set_location(Location(middle_2_2_range.begin(), kMiddleInline2FileLine, 0,
                                  SymbolContext::ForRelativeAddresses(), middle2_2_func));

  // Send the software breakpoint exception for PHYSICAL to finish.
  debug_ipc::BreakpointStats breakpoint{
      .id = static_cast<uint32_t>(mock_remote_api()->last_breakpoint_id()), .hit_count = 1};
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSoftwareBreakpoint,
                           MockFrameVectorToFrameVector(std::move(stack)), true, {breakpoint});
  // That should have finished PHYSICAL (deleting the temporary breakpoint) and OVER#1. Then started
  // stepping over OVER#2 which should continue.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(1, mock_remote_api()->breakpoint_remove_count());
}

// Tests that compiler generated ("line 0") code immediately following a function call is skipped
// when finishing a frame.
TEST_F(FinishThreadControllerTest, FinishToCompilerGenerated) {
  // This finishes the top inline frame of the default stack because it's the most convenient
  // thing to do.

  // Full stack for the starting point.
  auto stack = GetStack();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(stack)), true);

  // Finish the top frame. This should continue through the inline.
  auto finish_controller = std::make_unique<FinishThreadController>(thread()->GetStack(), 0);
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Set up line table information for the location immediately after the inline. It consists of
  // a "line 0" region followed by a regular region.
  const uint64_t kLine0Begin = kTopInlineFunctionRange.end();
  const uint64_t kNormalLineBegin = kLine0Begin + 4;
  module_symbols()->AddLineDetails(
      kLine0Begin,
      LineDetails(FileLine("", 0),
                  {LineDetails::LineEntry(AddressRange(kLine0Begin, kNormalLineBegin))}));
  FileLine normal_file_line("file.cc", 27);
  module_symbols()->AddLineDetails(
      kNormalLineBegin,
      LineDetails(normal_file_line,
                  {LineDetails::LineEntry(AddressRange(kNormalLineBegin, kNormalLineBegin + 4))}));

  // Inject an exception at the end of the inline frame. The controller should continue from here.
  stack = GetStack();
  stack.erase(stack.begin());  // Remove inline frame to leave the "top" physical frame at the top.
  Location old_top_location = stack[0]->GetLocation();
  stack[0]->set_location(Location(kLine0Begin, FileLine("", 0), 0,
                                  old_top_location.symbol_context(), old_top_location.symbol()));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(stack)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Now do an exception at the normal line region following it. The controller should stop.
  stack = GetStack();
  stack.erase(stack.begin());  // Remove inline frame to leave the "top" physical frame at the top.
  old_top_location = stack[0]->GetLocation();
  stack[0]->set_location(Location(kNormalLineBegin, normal_file_line, 0,
                                  old_top_location.symbol_context(), old_top_location.symbol()));
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(stack)), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped.
}

}  // namespace zxdb
