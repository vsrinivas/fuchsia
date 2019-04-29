// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/finish_physical_frame_thread_controller.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_impl_test_support.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

namespace {

constexpr uint64_t kInitialAddress = 0x12345678;
constexpr uint64_t kInitialBase = 0x1000;
constexpr uint64_t kReturnAddress = 0x34567890;
constexpr uint64_t kReturnBase = 0x1010;

class FinishPhysicalFrameThreadControllerTest
    : public InlineThreadControllerTest {
 public:
  // Creates a break notification with two stack frames using the constants
  // above.
  debug_ipc::NotifyException MakeBreakNotification() {
    debug_ipc::NotifyException n;

    n.type = debug_ipc::NotifyException::Type::kSoftware;
    n.thread.process_koid = process()->GetKoid();
    n.thread.thread_koid = thread()->GetKoid();
    n.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
    n.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;
    n.thread.frames.emplace_back(kInitialAddress, kInitialBase, kInitialBase);
    n.thread.frames.emplace_back(kReturnAddress, kReturnBase, kReturnBase);

    return n;
  }
};

}  // namespace

TEST_F(FinishPhysicalFrameThreadControllerTest, Finish) {
  // Notify of thread stop.
  auto break_notification = MakeBreakNotification();
  auto& break_frames = break_notification.thread.frames;
  InjectException(break_notification);

  constexpr uint64_t kBottomBase = kReturnBase + 0x10;
  debug_ipc::StackFrame bottom_frame(kReturnAddress, kBottomBase, kBottomBase);

  // Supply three frames for when the thread requests them: the top one (of the
  // stop above), the one we'll return to, and the one before that (so the
  // fingerprint of the one to return to can be computed). This stack value
  // should be larger than above (stack grows downward).
  debug_ipc::ThreadStatusReply expected_reply;
  // Copy previous frames and add to it.
  expected_reply.record = break_notification.thread;
  expected_reply.record.stack_amount =
      debug_ipc::ThreadRecord::StackAmount::kFull;
  expected_reply.record.frames.push_back(bottom_frame);
  mock_remote_api()->set_thread_status_reply(expected_reply);

  EXPECT_EQ(0, mock_remote_api()->breakpoint_add_count());
  Err out_err;
  mock_remote_api()->set_resume_quits_loop(true);
  thread()->ContinueWith(std::make_unique<FinishPhysicalFrameThreadController>(
                             thread()->GetStack(), 0),
                         [&out_err](const Err& err) {
                           out_err = err;
                           debug_ipc::MessageLoop::Current()->QuitNow();
                         });
  loop().Run();

  TestThreadObserver thread_observer(thread());

  // Finish should have added a temporary breakpoint at the return address.
  // The particulars of this may change with the implementation, but it's worth
  // testing to make sure the breakpoints are all hooked up to the stepping
  // properly.
  ASSERT_EQ(1, mock_remote_api()->breakpoint_add_count());
  ASSERT_EQ(kReturnAddress, mock_remote_api()->last_breakpoint_address());
  ASSERT_EQ(0, mock_remote_api()->breakpoint_remove_count());

  // Simulate a hit of the breakpoint. This stack frame is a recursive call
  // above the frame we're returning to so it should not trigger.
  break_frames.emplace(break_frames.begin(), kReturnAddress,
                       kInitialBase - 0x100, kInitialBase - 0x100);
  break_notification.hit_breakpoints.emplace_back();
  break_notification.hit_breakpoints[0].id =
      mock_remote_api()->last_breakpoint_id();
  InjectException(break_notification);
  EXPECT_FALSE(thread_observer.got_stopped());

  // Simulate a breakpoint hit with a lower BP (erase the two top ones = the
  // recursive call and the old top one). Need to add the bottom frame so there
  // are two (for computing the fingerprint).
  break_frames.erase(break_frames.begin(), break_frames.begin() + 2);
  break_frames.push_back(bottom_frame);
  InjectException(break_notification);
  EXPECT_TRUE(thread_observer.got_stopped());
  EXPECT_EQ(1, mock_remote_api()->breakpoint_remove_count());
}

// Tests "finish" at the bottom stack frame. Normally there's a stack frame
// with an IP of 0 below the last "real" stack frame.
TEST_F(FinishPhysicalFrameThreadControllerTest, BottomStackFrame) {
  // Notify of thread stop. Here we have the 0th frame of the current
  // location, and a null frame.
  auto break_notification = MakeBreakNotification();
  break_notification.thread.frames[1] = debug_ipc::StackFrame(0, 0, 0);
  InjectException(break_notification);

  // The backtrace reply gives the same two frames since that's all there is
  // (the Thread doesn't know until it requests them).
  debug_ipc::ThreadStatusReply expected_reply;
  expected_reply.record = break_notification.thread;
  expected_reply.record.stack_amount =
      debug_ipc::ThreadRecord::StackAmount::kFull;
  mock_remote_api()->set_thread_status_reply(expected_reply);

  EXPECT_EQ(0, mock_remote_api()->breakpoint_add_count());
  Err out_err;
  mock_remote_api()->set_resume_quits_loop(true);
  thread()->ContinueWith(std::make_unique<FinishPhysicalFrameThreadController>(
                             thread()->GetStack(), 0),
                         [&out_err](const Err& err) {
                           out_err = err;
                           debug_ipc::MessageLoop::Current()->QuitNow();
                         });
  loop().Run();

  TestThreadObserver thread_observer(thread());

  // Since the return address is null, we should not have attempted to create
  // a breakpoint, and the thread should have been resumed.
  ASSERT_EQ(0, mock_remote_api()->breakpoint_add_count());
  ASSERT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
}

// Finishing a physical frame should leave the stack at the calling frame. But
// the instruction after the function call being finished could be the first
// instruction of an inlined function (an ambiguous location -- see discussions
// in Stack class).
//
// In the case of ambiguity, the finish controller should leave the frame at
// the one that called the function being finished, not an inline frame that
// starts right atfer the call.

TEST_F(FinishPhysicalFrameThreadControllerTest, FinishToInline) {
  auto mock_frames = GetStack();

  // Save the return address from frame 1 (frame 2's IP).
  const uint64_t return_address = mock_frames[2]->GetAddress();

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);
  Stack& stack = thread()->GetStack();

  // Finish stack frame #1 (top physical frame).
  thread()->ContinueWith(
      std::make_unique<FinishPhysicalFrameThreadController>(stack, 1),
      [](const Err& err) {});
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continued.

  // Should have added a breakpoint to catch completion of function
  ASSERT_EQ(1, mock_remote_api()->breakpoint_add_count());
  ASSERT_EQ(return_address, mock_remote_api()->last_breakpoint_address());
  ASSERT_EQ(0, mock_remote_api()->breakpoint_remove_count());

  // Make breakpoint hit notification.
  std::vector<debug_ipc::BreakpointStats> hit_breakpoints;
  hit_breakpoints.emplace_back();
  hit_breakpoints[0].id = mock_remote_api()->last_breakpoint_id();

  // Make an inline function starting at the return address of the function.
  AddressRange second_inline_range(return_address, return_address + 4);
  auto second_inline_func =
      fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  second_inline_func->set_assigned_name("Second");
  second_inline_func->set_code_ranges(AddressRanges(second_inline_range));

  Location second_inline_loc(
      second_inline_range.begin(), FileLine("file.cc", 21), 0,
      SymbolContext::ForRelativeAddresses(), LazySymbol(second_inline_func));

  // Construct the stack of the address after the call. In this case the frame
  // being returned to immediately calls an inline subroutine, so execution
  // will be in a new inline function off of the returned-to frame.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin(), mock_frames.begin() + 2);
  mock_frames.insert(
      mock_frames.begin(),
      std::make_unique<MockFrame>(
          nullptr, nullptr,
          debug_ipc::StackFrame(second_inline_range.begin(), kMiddleSP,
                                kMiddleSP),
          second_inline_loc, mock_frames[0]->GetPhysicalFrame(), true));

  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true, hit_breakpoints);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stopped.

  EXPECT_EQ(1u, thread()->GetStack().hide_ambiguous_inline_frame_count());
}

}  // namespace zxdb
