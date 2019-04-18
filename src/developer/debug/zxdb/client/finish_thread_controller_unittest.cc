// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_impl_test_support.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

namespace {

class FinishThreadControllerTest : public InlineThreadControllerTest {};

}  // namespace

// See also the FinishPhysicalFrameThreadController tests.

// Tests finishing a single inline frame. This finishes the top frame of the
// stack which is an inline function (see InlineThreadControllerTest for what
// the returned stack layout is).
TEST_F(FinishThreadControllerTest, FinishInline) {
  auto mock_frames = GetStack();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // "Finish" from the top stack frame, which is an inline one.
  auto finish_controller =
      std::make_unique<FinishThreadController>(thread()->GetStack(), 0);
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller),
                         [&continued](const Err& err) {
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
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // That's still inside the frame's range, so it should continue.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Set exception at the first instruction after the inline frame.
  mock_frames = GetStack();
  uint64_t after_inline = kTopInlineFunctionRange.end();
  mock_frames.erase(mock_frames.begin());  // Remove the inline function.
  mock_frames[0]->SetAddress(after_inline);

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // Should not have resumed.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kBlocked, thread()->GetState());
}

// Finishes multiple frames, consisting of one physical frame finish followed
// by two inline frame finishes. This finishes to frame 4 (see
// InlineThreadControllerTest) which is the "middle" physical frame. It
// requires doing a "finish" of the top physical frame, then stepping through
// both middle inline frames.
TEST_F(FinishThreadControllerTest, FinishPhysicalAndInline) {
  auto mock_frames = GetStack();
  uint64_t frame_2_ip = mock_frames[2]->GetAddress();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // "Finish" frame 3,
  auto finish_controller =
      std::make_unique<FinishThreadController>(thread()->GetStack(), 3);
  bool continued = false;
  thread()->ContinueWith(std::move(finish_controller),
                         [&continued](const Err& err) {
                           if (!err.has_error())
                             continued = true;
                         });

  // That should have sent a resume + a breakpoint set at the frame 2 IP (this
  // breakpoint is implementing the "finish" to step out of the frame 1
  // physical frame).
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0, mock_remote_api()->breakpoint_remove_count());
  EXPECT_EQ(frame_2_ip, mock_remote_api()->last_breakpoint_address());

  // Simulate a breakpoint hit of that breakpoint (breakpoint exceptions are
  // "software").
  debug_ipc::NotifyException exception;
  exception.type = debug_ipc::NotifyException::Type::kSoftware;
  exception.thread.process_koid = process()->GetKoid();
  exception.thread.thread_koid = thread()->GetKoid();
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.hit_breakpoints.emplace_back();
  exception.hit_breakpoints[0].breakpoint_id =
      mock_remote_api()->last_breakpoint_id();

  // Create a stack now showing frame 2 as the top (new frame 0).
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 3);
  InjectExceptionWithStack(
      exception, MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // The breakpoint should have been cleared and the thread should have been
  // resumed.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(1, mock_remote_api()->breakpoint_remove_count());

  // Do another stop 4 bytes later in the inline frame 2 which should get
  // continued.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 3);
  mock_frames[0]->SetAddress(mock_frames[0]->GetAddress() + 4);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop in inline frame 1. This leaves inline frame 2 (right after its
  // address range) but should still continue since we haven't reached the
  // target.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 4);
  mock_frames[0]->SetAddress(kMiddleInline2FunctionRange.end());
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Stop in middle frame which is the target (right after the inline 1 range).
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 5);
  mock_frames[0]->SetAddress(kMiddleInline1FunctionRange.end());
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped.
}

}  // namespace zxdb
