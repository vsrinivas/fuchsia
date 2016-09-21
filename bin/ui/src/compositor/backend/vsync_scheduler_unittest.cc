// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/backend/vsync_scheduler.h"

#include <queue>

#include "base/bind.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/tick_clock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace compositor {

namespace {
constexpr int64_t kVsyncTimebase = -5000;
constexpr int64_t kVsyncInterval = 10000;
constexpr int64_t kUpdatePhase = -9000;
constexpr int64_t kSnapshotPhase = -1000;
constexpr int64_t kPresentationPhase = 2000;
}  // namespace

class VsyncSchedulerTest : public testing::Test {
 protected:
  void SetUp() override { Reset(); }

  void TearDown() override {
    task_runner_->FastForwardUntilNoTasksRemain();
    EXPECT_TRUE(expected_callbacks_.empty());
  }

  void ExpectUpdateCallback(int64_t frame_time,
                            uint64_t frame_interval,
                            int64_t frame_deadline,
                            int64_t presentation_time) {
    expected_callbacks_.emplace(CallbackType::kUpdate, frame_time, frame_time,
                                frame_interval, frame_deadline,
                                presentation_time);
  }

  void ExpectSnapshotCallback(int64_t frame_time,
                              uint64_t frame_interval,
                              int64_t frame_deadline,
                              int64_t presentation_time) {
    expected_callbacks_.emplace(CallbackType::kSnapshot, frame_deadline,
                                frame_time, frame_interval, frame_deadline,
                                presentation_time);
  }

  MojoTimeTicks GetTimeTicksNow() {
    return task_runner_->NowTicks().ToInternalValue();
  }

  void Reset() {
    task_runner_ = new base::TestMockTimeTaskRunner();
    SchedulerCallbacks callbacks(
        base::Bind(&VsyncSchedulerTest::OnUpdate, base::Unretained(this)),
        base::Bind(&VsyncSchedulerTest::OnSnapshot, base::Unretained(this)));
    scheduler_ =
        new VsyncScheduler(task_runner_, callbacks,
                           base::Bind(&VsyncSchedulerTest::GetTimeTicksNow,
                                      base::Unretained(this)));

    std::queue<ExpectedCallback> victim;
    expected_callbacks_.swap(victim);
  }

  void FastForwardTo(int64_t time) {
    FTL_DCHECK(time >= GetTimeTicksNow());
    task_runner_->FastForwardBy(
        base::TimeDelta::FromMicroseconds(time - GetTimeTicksNow()));
  }

  ftl::RefPtr<VsyncScheduler> scheduler_;

 private:
  enum class CallbackType {
    kUpdate,
    kSnapshot,
  };

  struct ExpectedCallback {
    ExpectedCallback(CallbackType type,
                     int64_t delivery_time,
                     int64_t frame_time,
                     uint64_t frame_interval,
                     int64_t frame_deadline,
                     int64_t presentation_time)
        : type(type),
          delivery_time(delivery_time),
          frame_time(frame_time),
          frame_interval(frame_interval),
          frame_deadline(frame_deadline),
          presentation_time(presentation_time) {}

    CallbackType type;
    int64_t delivery_time;
    int64_t frame_time;
    uint64_t frame_interval;
    int64_t frame_deadline;
    int64_t presentation_time;
  };

  void OnUpdate(const mojo::gfx::composition::FrameInfo& frame_info) {
    VerifyCallback(CallbackType::kUpdate, frame_info);
  }

  void OnSnapshot(const mojo::gfx::composition::FrameInfo& frame_info) {
    VerifyCallback(CallbackType::kSnapshot, frame_info);
  }

  void VerifyCallback(CallbackType type,
                      const mojo::gfx::composition::FrameInfo& frame_info) {
    EXPECT_FALSE(expected_callbacks_.empty());
    if (!expected_callbacks_.empty()) {
      const ExpectedCallback& c = expected_callbacks_.front();
      EXPECT_EQ(static_cast<int>(c.type), static_cast<int>(type));
      EXPECT_EQ(c.delivery_time, GetTimeTicksNow());
      EXPECT_EQ(c.frame_time, frame_info.frame_time);
      EXPECT_EQ(c.frame_interval, frame_info.frame_interval);
      EXPECT_EQ(c.frame_deadline, frame_info.frame_deadline);
      EXPECT_EQ(c.presentation_time, frame_info.presentation_time);
      expected_callbacks_.pop();
    }
  }

  ftl::RefPtr<base::TestMockTimeTaskRunner> task_runner_;
  std::queue<ExpectedCallback> expected_callbacks_;
};

TEST_F(VsyncSchedulerTest, StartValidatesArguments) {
  // Vsync timebase is in the past.
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));
  Reset();

  // Vsync timebase is now.  (current time == 0)
  EXPECT_TRUE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase, kSnapshotPhase,
                                kPresentationPhase));
  Reset();

  // Vsync timebase in the future.  (current time == 0)
  EXPECT_FALSE(scheduler_->Start(1, kVsyncInterval, kUpdatePhase,
                                 kSnapshotPhase, kPresentationPhase));

  // Vsync interval too small.
  EXPECT_FALSE(
      scheduler_->Start(0, VsyncScheduler::kMinVsyncInterval - 1, 0, 0, 0));

  // Vsync interval at minimum.
  EXPECT_TRUE(scheduler_->Start(0, VsyncScheduler::kMinVsyncInterval, 0, 0, 0));
  Reset();

  // Vsync interval at maximum.
  EXPECT_TRUE(scheduler_->Start(0, VsyncScheduler::kMaxVsyncInterval, 0, 0, 0));
  Reset();

  // Vsync interval too large.
  EXPECT_FALSE(
      scheduler_->Start(0, VsyncScheduler::kMaxVsyncInterval + 1, 0, 0, 0));

  // Snapshot phase earlier than update phase.
  EXPECT_FALSE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase,
                                 kUpdatePhase - 1, kPresentationPhase));

  // Snapshot phase more than one frame behind update phase.
  EXPECT_FALSE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase,
                                 kUpdatePhase + kVsyncInterval + 1,
                                 kPresentationPhase));

  // Presentation phase earlier than snapshot phase.
  EXPECT_FALSE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase,
                                 kSnapshotPhase, kSnapshotPhase - 1));

  // Minimum and maximum update vs. snapshot phase delta.
  EXPECT_TRUE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase, kUpdatePhase,
                                kUpdatePhase));
  Reset();
  EXPECT_TRUE(scheduler_->Start(0, kVsyncInterval, kUpdatePhase,
                                kUpdatePhase + kVsyncInterval,
                                kUpdatePhase + kVsyncInterval));
  Reset();
}

TEST_F(VsyncSchedulerTest, ScheduleRedundantSnapshot) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Shortly after the first update, schedule another snapshot.
  // Nothing happens because a snapshot is still due at 14000.
  FastForwardTo(8000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kSnapshot);
}

TEST_F(VsyncSchedulerTest, ScheduleRedundantUpdate) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Before the first update, schedule another update.
  // Nothing happens because an update is still due at 6000.
  FastForwardTo(5000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);
}

TEST_F(VsyncSchedulerTest, ScheduleRequiredSnapshot) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Shortly after the last snapshot, schedule another snapshot.
  FastForwardTo(15000);
  ExpectUpdateCallback(16000, kVsyncInterval, 24000, 27000);
  ExpectSnapshotCallback(16000, kVsyncInterval, 24000, 27000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kSnapshot);

  // Exactly at the moment of the next snapshot, schedule another snapshot.
  FastForwardTo(24000);
  ExpectUpdateCallback(26000, kVsyncInterval, 34000, 37000);
  ExpectSnapshotCallback(26000, kVsyncInterval, 34000, 37000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kSnapshot);

  // A long time thereafter, with no time to update, schedule another snapshot.
  FastForwardTo(53000);
  ExpectSnapshotCallback(46000, kVsyncInterval, 54000, 57000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kSnapshot);

  // A long time thereafter, with time to update, schedule another snapshot.
  FastForwardTo(75000);
  ExpectUpdateCallback(76000, kVsyncInterval, 84000, 87000);
  ExpectSnapshotCallback(76000, kVsyncInterval, 84000, 87000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kSnapshot);
}

TEST_F(VsyncSchedulerTest, ScheduleRequiredUpdate) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Shortly after the first update, schedule another update.
  FastForwardTo(8000);
  ExpectUpdateCallback(16000, kVsyncInterval, 24000, 27000);
  ExpectSnapshotCallback(16000, kVsyncInterval, 24000, 27000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // Exactly at the moment of the next update, schedule another update.
  FastForwardTo(16000);
  ExpectUpdateCallback(26000, kVsyncInterval, 34000, 37000);
  ExpectSnapshotCallback(26000, kVsyncInterval, 34000, 37000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // A long time thereafter, with no time to snapshot, schedule another update.
  FastForwardTo(55000);
  ExpectUpdateCallback(56000, kVsyncInterval, 64000, 67000);
  ExpectSnapshotCallback(56000, kVsyncInterval, 64000, 67000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // A long time thereafter, with time to snapshot, schedule another update.
  FastForwardTo(83000);
  ExpectSnapshotCallback(76000, kVsyncInterval, 84000, 87000);
  ExpectUpdateCallback(86000, kVsyncInterval, 94000, 97000);
  ExpectSnapshotCallback(86000, kVsyncInterval, 94000, 97000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);
}

TEST_F(VsyncSchedulerTest, StartAndStop) {
  // Scheduling frames before start does nothing.
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // Starting the scheduler automatically schedules an update.
  FastForwardTo(15000);
  ExpectUpdateCallback(16000, kVsyncInterval, 24000, 27000);
  ExpectSnapshotCallback(16000, kVsyncInterval, 24000, 27000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Stopping the scheduler suspends further updates.
  FastForwardTo(24000);
  scheduler_->Stop();
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // Restarting scheduling resumes updates.
  FastForwardTo(53000);
  ExpectSnapshotCallback(46000, kVsyncInterval, 54000, 57000);
  ExpectUpdateCallback(56000, kVsyncInterval, 64000, 67000);
  ExpectSnapshotCallback(56000, kVsyncInterval, 64000, 67000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Stopping the scheduler cancels undelivered updates.
  FastForwardTo(63000);
  // canceled: ExpectUpdateCallback(66000, kVsyncInterval, 74000, 77000);
  // canceled: ExpectSnapshotCallback(66000, kVsyncInterval, 74000, 77000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);
  FastForwardTo(65000);
  scheduler_->Stop();
}

TEST_F(VsyncSchedulerTest, RedundantStart) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // Doing it again has no added effect.
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // A long time thereafter, schedule another update.
  FastForwardTo(55000);
  ExpectUpdateCallback(56000, kVsyncInterval, 64000, 67000);
  ExpectSnapshotCallback(56000, kVsyncInterval, 64000, 67000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);
}

TEST_F(VsyncSchedulerTest, StartWithNewParameters) {
  // Start immediately schedules work.
  ExpectSnapshotCallback(-4000, kVsyncInterval, 4000, 7000);
  ExpectUpdateCallback(6000, kVsyncInterval, 14000, 17000);
  ExpectSnapshotCallback(6000, kVsyncInterval, 14000, 17000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));

  // After the snapshot is delivered, change parameters.
  FastForwardTo(14000);
  ExpectUpdateCallback(17000, kVsyncInterval * 2, 33000, 39000);
  ExpectSnapshotCallback(17000, kVsyncInterval * 2, 33000, 39000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval * 2,
                                kUpdatePhase * 2, kSnapshotPhase * 2,
                                kPresentationPhase * 2));

  // Schedule another update with these parameters.
  FastForwardTo(18000);
  ExpectUpdateCallback(37000, kVsyncInterval * 2, 53000, 59000);
  // canceled: ExpectSnapshotCallback(37000, kVsyncInterval * 2, 53000, 59000);
  scheduler_->ScheduleFrame(Scheduler::SchedulingMode::kUpdateAndSnapshot);

  // At the moment when the update is delivered, change parameters again.
  // We're too late to cancel the prior update but we do cancel the prior
  // snapshot and we'll follow it up with another update with the new
  // parameters.  We also skip ahead a little bit to preserve monotonicity
  // of the presentation time.
  FastForwardTo(37000);
  ExpectUpdateCallback(56000, kVsyncInterval, 64000, 67000);
  ExpectSnapshotCallback(56000, kVsyncInterval, 64000, 67000);
  EXPECT_TRUE(scheduler_->Start(kVsyncTimebase, kVsyncInterval, kUpdatePhase,
                                kSnapshotPhase, kPresentationPhase));
}

// TODO(jeffbrown): Add tests for cases where the compositor has fallen behind.

}  // namespace compositor
