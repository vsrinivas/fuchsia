// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/until_thread_controller.h"

#include "src/developer/debug/zxdb/client/inline_thread_controller_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace zxdb {

// This test doesn't test any inline functionality but it's convenient to re-use that class'
// default stack setup.
using UntilThreadControllerTest = InlineThreadControllerTest;

namespace {

// Wrapper around UntilThreadController that reports when it's destroyed.
class TrackedUntilThreadController : public UntilThreadController {
 public:
  TrackedUntilThreadController(bool* destroyed_flag, std::vector<InputLocation> locations)
      : UntilThreadController(std::move(locations)), destroyed_flag_(destroyed_flag) {}
  ~TrackedUntilThreadController() { *destroyed_flag_ = true; }

 private:
  bool* destroyed_flag_;
};

}  // namespace

TEST_F(UntilThreadControllerTest, Basic) {
  // Report a stop at the source address.
  auto mock_frames = GetStack();
  uint64_t start_address = mock_frames[0]->GetAddress();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  bool controller_destroyed = false;

  // Continue "until" an address a few bytes later.
  uint64_t dest_address = start_address + 32;
  thread()->ContinueWith(
      std::make_unique<TrackedUntilThreadController>(
          &controller_destroyed, std::vector<InputLocation>{InputLocation(dest_address)}),
      [](const Err& err) {});
  EXPECT_FALSE(controller_destroyed);

  // The thread should have continued.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Report a software breakpoint stop at the target address.
  mock_frames = GetStack();
  mock_frames[0]->SetAddress(dest_address);
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::ExceptionType::kSoftwareBreakpoint,
                           MockFrameVectorToFrameVector(std::move(mock_frames)), true);

  // Should not have continued and the controller should be done.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());  // Stop.
  EXPECT_TRUE(controller_destroyed);
}

}  // namespace zxdb
