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
                                          /* target presentation time*/ 1,
                                          /* render started time */ 0);
    frame_timings_->AddSwapchain(nullptr);
  }
  void TearDown() override { frame_scheduler_.reset(); }

  fxl::RefPtr<FrameTimings> frame_timings_;
  std::unique_ptr<MockFrameScheduler> frame_scheduler_;
};

TEST_F(FrameTimingsTest,
       ReceivingCallsInOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(/* swapchain_index */ 0, /* time */ 1);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(/* swapchain_index */ 0, /* time */ 2);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_LE(frame_timings_->rendering_finished_time(),
            frame_timings_->actual_presentation_time());
}

TEST_F(FrameTimingsTest,
       ReceivingCallsOutOfOrder_ShouldTriggerFrameSchedulerCallsInOrder) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFramePresented(/* swapchain_index */ 0, /* time */ 2);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameRendered(/* swapchain_index */ 0, /* time */ 3);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  // Rendering should never seem to have finished after presentation.
  EXPECT_LE(frame_timings_->rendering_finished_time(),
            frame_timings_->actual_presentation_time());
}

TEST_F(FrameTimingsTest,
       FrameDroppedAfterRender_ShouldNotTriggerSecondFrameRenderedCall) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  const zx_time_t render_finished_time = 2;

  frame_timings_->OnFrameRendered(/* swapchain_index */ 0,
                                  /* time */ render_finished_time);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);
  EXPECT_FALSE(frame_timings_->frame_was_dropped());

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 1u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_EQ(frame_timings_->rendering_finished_time(), render_finished_time);
  EXPECT_TRUE(frame_timings_->frame_was_dropped());
}

TEST_F(FrameTimingsTest,
       FrameDroppedBeforeRender_ShouldNotTriggerFrameRenderedCall) {
  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 0u);

  frame_timings_->OnFrameDropped(/* swapchain_index */ 0);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);
  EXPECT_TRUE(frame_timings_->frame_was_dropped());

  frame_timings_->OnFrameRendered(/* swapchain_index */ 0, /* time */ 3);

  EXPECT_EQ(frame_scheduler_->frame_rendered_call_count(), 0u);
  EXPECT_EQ(frame_scheduler_->frame_presented_call_count(), 1u);

  EXPECT_EQ(frame_timings_->rendering_finished_time(), ZX_TIME_INFINITE);
  EXPECT_TRUE(frame_timings_->frame_was_dropped());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
