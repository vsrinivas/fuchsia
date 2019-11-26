// Copyright 2019 The Fuchsia Authors-> All rights reserved->
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file->

#include "src/developer/feedback/feedback_agent/ref_counted_delayed_task.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

// The delay can be arbitrarily long as we are using a fake clock.
constexpr zx::duration kDelay = zx::min(10);

class RefCountedDelayedTaskTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    task_ = std::make_unique<RefCountedDelayedTask>(
        dispatcher(), [this] { task_completed_ = true; }, kDelay);
  }

 protected:
  std::unique_ptr<RefCountedDelayedTask> task_;
  bool task_completed_ = false;
};

TEST_F(RefCountedDelayedTaskTest, FailToRelease_OnZeroAcquires) {
  EXPECT_EQ(task_->Release(), ZX_ERR_BAD_STATE);
}

TEST_F(RefCountedDelayedTaskTest, Check_TaskNotScheduled_OnOneAcquireZeroReleases) {
  task_->Acquire();

  ASSERT_FALSE(task_completed_);

  // Run for longer than the task's delay and check that the task is still not completed.
  ASSERT_FALSE(RunLoopFor(kDelay * 2));
  EXPECT_FALSE(task_completed_);
}

TEST_F(RefCountedDelayedTaskTest, Check_TaskCompletes_OnOneAcquireOneRelease) {
  task_->Acquire();
  task_->Release();

  ASSERT_FALSE(task_completed_);

  // Run for the task's delay and check that the task is completed.
  ASSERT_TRUE(RunLoopFor(kDelay));
  EXPECT_TRUE(task_completed_);
}

TEST_F(RefCountedDelayedTaskTest, Check_TaskNotScheduled_OnTwoAcquiresOneRelease) {
  task_->Acquire();
  task_->Acquire();

  task_->Release();

  ASSERT_FALSE(task_completed_);

  // Run for longer than the task's delay and check that the task is still not completed.
  ASSERT_FALSE(RunLoopFor(kDelay * 2));
  EXPECT_FALSE(task_completed_);
}

TEST_F(RefCountedDelayedTaskTest, Check_TaskCompletes_OnTwoAcquiresTwoReleases) {
  task_->Acquire();
  task_->Acquire();

  task_->Release();
  task_->Release();

  ASSERT_FALSE(task_completed_);

  // Run for the task's delay and check that the task is completed.
  ASSERT_TRUE(RunLoopFor(kDelay));
  EXPECT_TRUE(task_completed_);
}

TEST_F(RefCountedDelayedTaskTest, Check_TaskStaysBlocked_TwoAcquires_DelayedRelease) {
  task_->Acquire();
  task_->Release();

  // Run for less than the task has to wait before being executed.
  ASSERT_FALSE(RunLoopFor(kDelay / 2));
  ASSERT_FALSE(task_completed_);

  task_->Acquire();

  ASSERT_FALSE(task_completed_);

  // Run for longer than the task's delay and check that the task is still not completed.
  ASSERT_TRUE(RunLoopFor(kDelay * 2));
  ASSERT_FALSE(task_completed_);

  task_->Release();

  // Run for the task's delay and check that the task is completed.
  ASSERT_TRUE(RunLoopFor(kDelay));
  EXPECT_TRUE(task_completed_);
}

}  // namespace
}  // namespace feedback
