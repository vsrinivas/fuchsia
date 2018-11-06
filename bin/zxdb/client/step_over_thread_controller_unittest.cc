// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_over_thread_controller.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/thread_controller_test.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class StepOverThreadControllerTest : public ThreadControllerTest {};

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
      AddressRange(kBeginAddr, kEndAddr));
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

}  // namespace zxdb
