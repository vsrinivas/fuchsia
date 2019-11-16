// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_timings.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scheduling {
namespace test {

class FrameTimingsTest : public ::gtest::TestLoopFixture {
 protected:
  // | ::testing::Test |
  void SetUp() override {
    frame_timings_ = std::make_unique<FrameTimings>(
        /* frame number */ 1, /* target presentation */ zx::time(1),
        /* latch point */ zx::time(0), /* render started */ zx::time(0),
        fit::bind_member(this, &FrameTimingsTest::OnFrameRendered),
        fit::bind_member(this, &FrameTimingsTest::OnFramePresented));
    frame_timings_->RegisterSwapchains(1);
    swapchain_index_ = 0;
  }

  void TearDown() override {
    frame_timings_ = nullptr;

    frame_presented_call_count_ = 0;
    frame_rendered_call_count_ = 0;
  }

  void OnFramePresented(const FrameTimings& timings) { ++frame_presented_call_count_; }
  void OnFrameRendered(const FrameTimings& timings) { ++frame_rendered_call_count_; }

  std::unique_ptr<FrameTimings> frame_timings_;
  size_t swapchain_index_;

  uint32_t frame_presented_call_count() { return frame_presented_call_count_; }
  uint32_t frame_rendered_call_count() { return frame_rendered_call_count_; }

 private:
  uint32_t frame_presented_call_count_ = 0;
  uint32_t frame_rendered_call_count_ = 0;
};

TEST_F(FrameTimingsTest, GetWeakPtr) {
  auto weak_timings = frame_timings_->GetWeakPtr();
  EXPECT_TRUE(weak_timings);

  frame_timings_.reset();

  EXPECT_FALSE(weak_timings);
}

TEST_F(FrameTimingsTest, ReceivingCallsInOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(1));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingCallsOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(5));

  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingCallsAndTimesOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingTimesOutOfOrder_ShouldRecordTimesInOrder) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, FrameDroppedAfterRender_ShouldNotTriggerSecondFrameRenderedCall) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  const zx::time render_finished_time = zx::time(2);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(render_finished_time));

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 0u);
  EXPECT_FALSE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
}

TEST_F(FrameTimingsTest, FrameDroppedBeforeRender_ShouldStillTriggerFrameRenderedCall) {
  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_presented_call_count(), 0u);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  const zx::time render_finished_time = zx::time(500);
  frame_timings_->OnFrameRendered(swapchain_index_, render_finished_time);

  EXPECT_EQ(frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_EQ(timestamps.actual_presentation_time, FrameTimings::kTimeDropped);
}

TEST_F(FrameTimingsTest, LargerRenderingCpuDuration_ShouldBeReturned) {
  frame_timings_->OnFrameRendered(0, zx::time(100));
  frame_timings_->OnFrameCpuRendered(zx::time(400));

  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST_F(FrameTimingsTest, LargerRenderingGpuDuration_ShouldBeReturned) {
  frame_timings_->OnFrameCpuRendered(zx::time(100));
  frame_timings_->OnFrameRendered(0, zx::time(400));

  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST_F(FrameTimingsTest, RenderingCpu_Duration_ShouldBeMaxed) {
  frame_timings_->OnFrameCpuRendered(zx::time(400));
  frame_timings_->OnFrameCpuRendered(zx::time(100));

  FrameTimings::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST(FrameTimings, DroppedAndUnitializedTimesAreUnique) {
  EXPECT_LT(FrameTimings::kTimeUninitialized, FrameTimings::kTimeDropped);
}

TEST(FrameTimings, InitTimestamps) {
  const zx::time target_present_time(16);
  const zx::time latch_time(10);
  const zx::time render_start_time(12);
  const uint64_t frame_number = 5;
  auto timings = std::make_unique<FrameTimings>(
      frame_number, target_present_time, latch_time,
      render_start_time, [](const FrameTimings& timings){},
      [](const FrameTimings& timings){});

  FrameTimings::Timestamps init_timestamps = timings->GetTimestamps();
  // Inputs should be recorded in the timestamps.
  EXPECT_EQ(init_timestamps.latch_point_time, latch_time);
  EXPECT_EQ(init_timestamps.render_start_time, render_start_time);
  EXPECT_EQ(init_timestamps.target_presentation_time, target_present_time);
  // The frame is not finalized, and none of the outputs have been recorded.
  EXPECT_FALSE(timings->finalized());
  EXPECT_EQ(init_timestamps.update_done_time, FrameTimings::kTimeUninitialized);
  EXPECT_EQ(init_timestamps.render_done_time, FrameTimings::kTimeUninitialized);
  EXPECT_EQ(init_timestamps.actual_presentation_time, FrameTimings::kTimeUninitialized);

  EXPECT_FALSE(timings->FrameWasDropped());
  EXPECT_EQ(frame_number, timings->frame_number());
}

}  // namespace test
}  // namespace scheduling
