// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_into_specific_thread_controller.h"

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"

namespace zxdb {

class StepIntoSpecificThreadControllerTest : public InlineThreadControllerTest {
 public:
  // Returns the stack with the "middle inline 2" frame at the top. This removes the top and "top
  // inline 2" frames from the default mock inline stack.
  auto GetStackAtMiddleInline2() const {
    auto frames = GetStack();
    frames.erase(frames.begin(), frames.begin() + 2);
    return frames;
  }
};

// For convenience this steps into the same function call twice (this lets us use the mock stack
// from the inline thread controller test). The first call is done from within the range so gets
// stepped over, the second call is the one we step into.
TEST_F(StepIntoSpecificThreadControllerTest, Step) {
  // Provide line information for the "top" physical frame which is where we want to stop. Otherwise
  // the stepper will continue through unsymbolized functions.
  const uint64_t kEndAddress = kTopFunctionRange.begin();
  module_symbols()->AddLineDetails(
      kEndAddress, LineDetails(kTopFileLine, {LineDetails::LineEntry(kTopFunctionRange)}));

  auto mock_frames = GetStackAtMiddleInline2();

  // The location we're stepping from is the middle frame.
  const uint64_t kFromAddress = mock_frames[0]->GetAddress();
  const uint64_t kToAddress = kFromAddress + 8;
  AddressRange range(kFromAddress, kToAddress);  // Range being stepped over.

  // Inject an exception at the top inline of the middle frame. It's about to call the top frame.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Step over the range and into the next function.
  thread()->ContinueWith(std::make_unique<StepIntoSpecificThreadController>(range),
                         [](const Err& err) {});
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Stop in a new stack frame called by the previous execution. It should continue.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Delete top inline to leave us at top (we don't need
                                           // the top inline for this test).
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Execution returns to the original "middle" frame at the next instruction.
  mock_frames = GetStackAtMiddleInline2();
  mock_frames[0]->SetAddress(kFromAddress + 1);  // Set to next instruction.
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Now exit the range.
  mock_frames = GetStackAtMiddleInline2();
  mock_frames[0]->SetAddress(kToAddress);  // End of range (is non-inclusive).
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());  // Continue.

  // Step into a new stack frame. Since we exited the range this is the "specific" function being
  // stepped into.
  mock_frames = GetStack();
  mock_frames.erase(mock_frames.begin());  // Delete top inline to leave us at top.
  mock_frames[0]->SetAddress(kEndAddress);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stop.
}

}  // namespace zxdb
