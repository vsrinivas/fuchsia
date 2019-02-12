// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/step_into_thread_controller.h"
#include "garnet/bin/zxdb/client/inline_thread_controller_test.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class StepIntoThreadControllerTest : public InlineThreadControllerTest {};

}  // namespace

TEST_F(StepIntoThreadControllerTest, Basic) {
  // Recall the top frame from GetStack() is inline.
  auto mock_frames = GetStack();
  InjectExceptionWithStack(process()->GetKoid(), thread()->GetKoid(),
                           debug_ipc::NotifyException::Type::kSingleStep,
                           MockFrameVectorToFrameVector(std::move(mock_frames)),
                           true);

  // Hide the inline frame at the top so we're about to step into it.
  Stack& stack = thread()->GetStack();
  stack.SetHideAmbiguousInlineFrameCount(1);

  // Do the "step into".
  auto step_into_controller =
      std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
  bool continued = false;
  thread()->ContinueWith(std::move(step_into_controller),
                         [&continued](const Err& err) {
                           if (!err.has_error())
                             continued = true;
                         });
  EXPECT_TRUE(continued);

  // The operation should have unhidden the inline stack frame rather than
  // actually affecting the backend.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());

  // Now that we're at the top of the inline stack, do a subsequent "step into"
  // which this time should resume the backend.
  step_into_controller =
      std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
  continued = false;
  thread()->ContinueWith(std::move(step_into_controller),
                         [&continued](const Err& err) {
                           if (!err.has_error())
                             continued = true;
                         });
  EXPECT_TRUE(continued);
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());
  EXPECT_EQ(0u, stack.hide_ambiguous_inline_frame_count());
}

}  // namespace zxdb
