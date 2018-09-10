// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_thread_controller.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/thread_controller_test.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class StepThreadControllerTest : public ThreadControllerTest {};

// Software exceptions should always stop execution. These might be from
// something like a hardcoded breakpoint instruction in the code. Doing "step"
// shouldn't skip over these.
TEST_F(StepThreadControllerTest, SofwareException) {
  // Step as long as we're in this range. Using the "code range" for stepping
  // allows us to avoid dependencies on the symbol subsystem.
  constexpr uint64_t kBeginAddr = 0x1000;
  constexpr uint64_t kEndAddr = 0x1010;

  // Set up the thread to be stopped at the beginning of our range.
  debug_ipc::NotifyException exception;
  exception.process_koid = process()->GetKoid();
  exception.type = debug_ipc::NotifyException::Type::kHardware;
  exception.thread.koid = thread()->GetKoid();
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.frames.resize(2);
  exception.frames[0].ip = kBeginAddr;
  exception.frames[0].sp = 0x5000;
  exception.frames[0].bp = 0x5000;
  InjectException(exception);

  // Continue the thread with the controller stepping in range.
  auto step_into = std::make_unique<StepThreadController>(
      AddressRange(kBeginAddr, kEndAddr));
  bool continued = false;
  thread()->ContinueWith(std::move(step_into), [&continued](const Err& err) {
    if (!err.has_error())
      continued = true;
  });

  // It should have been able to step without doing any further async work.
  EXPECT_TRUE(continued);
  EXPECT_EQ(1, resume_count());

  // Issue a software exception in the range.
  exception.type = debug_ipc::NotifyException::Type::kSoftware;
  exception.frames[0].ip += 4;
  InjectException(exception);

  // It should have stayed stopped despite being in range.
  EXPECT_EQ(1, resume_count());  // Same count as above.
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kBlocked, thread()->GetState());
}

}  // namespace zxdb
