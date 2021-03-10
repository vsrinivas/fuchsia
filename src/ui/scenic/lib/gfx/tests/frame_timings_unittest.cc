// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/frame_timings.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/gtest/test_loop_fixture.h>

using scheduling::FrameRenderer;

namespace scenic_impl {
namespace gfx {
namespace test {

class FrameTimingsTest : public ::gtest::TestLoopFixture {
 protected:
  // | ::testing::Test |
  void SetUp() override {
    frame_timings_ = std::make_unique<FrameTimings>(
        /* frame number */ 1, fit::bind_member(this, &FrameTimingsTest::OnFramePresented));
    frame_timings_->RegisterSwapchains(1);
    swapchain_index_ = 0;
  }

  void TearDown() override {
    frame_timings_ = nullptr;

    frame_presented_call_count_ = 0;
  }

  void OnFramePresented(const FrameTimings& timings) { ++frame_presented_call_count_; }

  std::unique_ptr<FrameTimings> frame_timings_;
  size_t swapchain_index_;

  uint32_t frame_presented_call_count() { return frame_presented_call_count_; }

 private:
  uint32_t frame_presented_call_count_ = 0;
};

TEST_F(FrameTimingsTest, ReceivingCallsInOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(1));

  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingCallsOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(5));

  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingCallsAndTimesOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, ReceivingTimesOutOfOrder_ShouldRecordTimesInOrder) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(3));

  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(swapchain_index_, zx::time(2));

  EXPECT_EQ(frame_presented_call_count(), 1u);

  // Rendering should never finish after presentation.
  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_LE(timestamps.render_done_time, timestamps.actual_presentation_time);
}

TEST_F(FrameTimingsTest, FrameDroppedAfterRender_ShouldNotTriggerSecondFrameRenderedCall) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  const zx::time render_finished_time = zx::time(2);

  frame_timings_->OnFrameRendered(swapchain_index_, zx::time(render_finished_time));

  EXPECT_EQ(frame_presented_call_count(), 0u);
  EXPECT_FALSE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
}

TEST_F(FrameTimingsTest, FrameDroppedBeforeRender_ShouldStillTriggerFrameRenderedCall) {
  EXPECT_EQ(frame_presented_call_count(), 0u);

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_presented_call_count(), 0u);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_FALSE(frame_timings_->finalized());

  const zx::time render_finished_time = zx::time(500);
  frame_timings_->OnFrameRendered(swapchain_index_, render_finished_time);

  EXPECT_EQ(frame_presented_call_count(), 1u);

  EXPECT_TRUE(frame_timings_->finalized());
  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, render_finished_time);
  EXPECT_TRUE(frame_timings_->FrameWasDropped());
  EXPECT_EQ(timestamps.actual_presentation_time, FrameRenderer::kTimeDropped);
}

TEST_F(FrameTimingsTest, FrameSkipped_ShouldStillTriggerPresentCallbacks) {
  // Reset the size of the swapchain. OnFrameSkipped() assumes that the
  // registered swapchain size is zero, since nothing is submitted for rendering.
  frame_timings_->RegisterSwapchains(0);

  EXPECT_EQ(frame_presented_call_count(), 0u);

  RunLoopFor(zx::sec(1) / 60);

  frame_timings_->OnFrameSkipped();

  EXPECT_EQ(frame_presented_call_count(), 1u);
  EXPECT_TRUE(frame_timings_->FrameWasSkipped());
  EXPECT_TRUE(frame_timings_->finalized());

  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, Now());
  EXPECT_EQ(timestamps.actual_presentation_time, Now());
}

TEST_F(FrameTimingsTest, LargerRenderingCpuDuration_ShouldBeReturned) {
  frame_timings_->OnFrameRendered(0, zx::time(100));
  frame_timings_->OnFrameCpuRendered(zx::time(400));

  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST_F(FrameTimingsTest, LargerRenderingGpuDuration_ShouldBeReturned) {
  frame_timings_->OnFrameCpuRendered(zx::time(100));
  frame_timings_->OnFrameRendered(0, zx::time(400));

  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST_F(FrameTimingsTest, RenderingCpu_Duration_ShouldBeMaxed) {
  frame_timings_->OnFrameCpuRendered(zx::time(400));
  frame_timings_->OnFrameCpuRendered(zx::time(100));

  FrameRenderer::Timestamps timestamps = frame_timings_->GetTimestamps();
  EXPECT_EQ(timestamps.render_done_time, zx::time(400));
}

TEST(FrameTimings, DroppedAndUnitializedTimesAreUnique) {
  EXPECT_LT(FrameTimings::kTimeUninitialized, FrameRenderer::kTimeDropped);
}

TEST(FrameTimings, InitTimestamps) {
  const uint64_t kFrameNumber = 5;
  auto timings = std::make_unique<FrameTimings>(kFrameNumber, [](const FrameTimings& timings) {});

  FrameRenderer::Timestamps init_timestamps = timings->GetTimestamps();
  // The frame is not finalized, and none of the outputs have been recorded.
  EXPECT_FALSE(timings->finalized());
  EXPECT_EQ(init_timestamps.render_done_time, FrameTimings::kTimeUninitialized);
  EXPECT_EQ(init_timestamps.actual_presentation_time, FrameTimings::kTimeUninitialized);

  EXPECT_FALSE(timings->FrameWasDropped());
  EXPECT_EQ(kFrameNumber, timings->frame_number());
}

TEST(FrameTimings, WaitForAllSwapchains) {
  const uint64_t kFrameNumber = 5;

  bool timings_done;
  std::unique_ptr<FrameTimings> timings;

  timings_done = false;
  timings = std::make_unique<FrameTimings>(
      kFrameNumber, [&timings_done](const FrameTimings& timings) { timings_done = true; });
  timings->RegisterSwapchains(2);

  EXPECT_FALSE(timings_done);
  timings->OnFrameRendered(/*swapchain_index=*/0, zx::time(200));
  timings->OnFramePresented(/*swapchain_index=*/0, zx::time(400));
  EXPECT_FALSE(timings_done);
  timings->OnFrameRendered(/*swapchain_index=*/1, zx::time(200));
  timings->OnFramePresented(/*swapchain_index=*/1, zx::time(400));
  EXPECT_TRUE(timings_done);

  timings_done = false;
  timings = std::make_unique<FrameTimings>(
      kFrameNumber, [&timings_done](const FrameTimings& timings) { timings_done = true; });
  timings->RegisterSwapchains(2);

  EXPECT_FALSE(timings_done);
  timings->OnFrameRendered(/*swapchain_index=*/0, zx::time(200));
  timings->OnFrameDropped(/*swapchain_index=*/0);
  EXPECT_FALSE(timings_done);
  timings->OnFrameRendered(/*swapchain_index=*/1, zx::time(200));
  timings->OnFrameDropped(/*swapchain_index=*/1);
  EXPECT_TRUE(timings_done);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
