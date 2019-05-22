// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/frame_timings.h"

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"
#include "garnet/lib/ui/gfx/tests/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class FrameTimingsTest : public ErrorReportingTest {
 protected:
  // | ::testing::Test |
  void SetUp() override {
    frame_scheduler_ = std::make_unique<MockFrameScheduler>();
    frame_timings_ =
        fxl::MakeRefCounted<FrameTimings>(frame_scheduler_.get(),
                                          /* frame number */ 1,
                                          /* target presentation time */ 1,
                                          /* latch_point_time */ 0,
                                          /* render started time */ 0);
    swapchain_index_ = frame_timings_->RegisterSwapchain();
  }
  void TearDown() override { frame_scheduler_.reset(); }

  fxl::RefPtr<FrameTimings> frame_timings_;
  std::unique_ptr<MockFrameScheduler> frame_scheduler_;
  size_t swapchain_index_;
};

TEST_F(FrameTimingsTest,
       ReceivingCallsInOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, /* time */ 1);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, /* time */ 2);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest,
       ReceivingCallsOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, /* time */ 5);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, /* time */ 3);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(
    FrameTimingsTest,
    ReceivingCallsAndTimesOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, /* time */ 2);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, /* time */ 3);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingTimesOutOfOrder_ShouldRecordTimesInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, /* time */ 3);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, /* time */ 2);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest,
       FrameDroppedAfterRender_ShouldNotTriggerSecondFrameRenderedCall) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  const zx_time_t render_finished_time = 2;

  frame_timings_->OnFrameRendered(swapchain_index_,
                                  /* time */ render_finished_time);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);
  EXPECT_FALSE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
}

TEST_F(FrameTimingsTest,
       FrameDroppedBeforeRender_ShouldStillTriggerFrameRenderedCall) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  const zx_time_t render_finished_time = zx::usec(500).get();
  frame_timings_->OnFrameRendered(swapchain_index_, render_finished_time);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_EQ(timestamps.actual_presentation_time, FrameTimings::kTimeDropped);
}

TEST(FrameTimings, DroppedAndUnitializedTimesAreUnique) {
  EXPECT_LT(FrameTimings::kTimeUninitialized, FrameTimings::kTimeDropped);
}

TEST(FrameTimings, InitTimestamps) {
  const zx_time_t target_present_time = zx::msec(16).get();
  const zx_time_t latch_time = zx::msec(10).get();
  const zx_time_t render_start_time = zx::msec(12).get();
  const uint64_t frame_number = 5;
  auto timings = fxl::MakeRefCounted<FrameTimings>(
      /* frame_scheduler */ nullptr, frame_number, target_present_time,
      latch_time, render_start_time);

  FrameTimings::Timestamps init_timestamps = timings->GetTimestamps();
  // Inputs should be recorded in the timestamps.
  EXPECT_EQ(init_timestamps.latch_point_time, latch_time);
  EXPECT_EQ(init_timestamps.render_start_time, render_start_time);
  EXPECT_EQ(init_timestamps.target_presentation_time, target_present_time);
  // The frame is not finalized, and none of the outputs have been recorded.
  EXPECT_FALSE(timings->finalized());
  EXPECT_EQ(init_timestamps.update_done_time, FrameTimings::kTimeUninitialized);
  EXPECT_EQ(init_timestamps.render_done_time, FrameTimings::kTimeUninitialized);
  EXPECT_EQ(init_timestamps.actual_presentation_time,
            FrameTimings::kTimeUninitialized);

  EXPECT_FALSE(timings->FrameWasDropped());
  EXPECT_EQ(frame_number, timings->frame_number());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
