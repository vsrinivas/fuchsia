// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/release_fence_manager.h"

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "zircon/system/public/zircon/syscalls.h"

// TEST COVERAGE NOTES
//
// There are quite a few cases to test here, and it is difficult to get an idea of the coverage
// by reading the code.  This is an overview of the cases that are tested below.
//
// 0) It's not useful for the client to pass release fences with frame #1, but we don't disallow it.
//    Since there is no previous frame, these fences are signaled immediately.
//
//    Tests:
//    - FirstFrameSignalsImmediately
//
// 1) Verify that the moment that release fence are signaled depends on whether the *previous* frame
//    is GPU-composited or direct-scanout.  See "Design Requirements" in the ReleaseFenceManager
//    class comment.
//
//    Tests:
//    - SignalingWhenPreviousFrameWasGpuComposited
//    - SignalingWhenPreviousFrameWasDirectScanout
//
// 2) Dropped/Skipped frames.  OnVsync() for later frame causes frame callback of earlier frames to
//    be invoked (assuming that all render_finished_fences are signaled for earlier GPU-composited
//    frames).
//
//    Tests:
//    - OutOfOrderRenderFinished
//
// 3) FrameRecords are removed ASAP, as soon as the frame callback has been invoked and there is at
//    least one subsequent frame registered.
//
//    Tests:
//    - ImmediateErasure
//
// 4) Repeated OnVsync() calls with the same frame number are OK.  This is an expected use case:
//    this is what will be received from the display controller and someone needs to handle it, so
//    might as well be ReleaseFenceManager.
//
//    Tests:
//    - RepeatedOnVsyncFrameNumbers
//
// 5) Edge-case where OnVsync() is received before |render_finished_fence| is signaled (or at least
//    before the signal is handled).
//
//    Tests:
//    - FramePresentedCallbackForGpuCompositedFrame
//
// 6) Properly-set timestamps in frame-presented callback.
//
//    Tests:
//    - OutOfOrderRenderFinished
//    - FramePresentedCallbackForGpuCompositedFrame
//    - FramePresentedCallbackForDirectScanoutFrame

namespace flatland::test {

namespace {

class ReleaseFenceManagerTest : public gtest::TestLoopFixture {};

}  // namespace

TEST_F(ReleaseFenceManagerTest, FirstFrameSignalsImmediately) {
  // Test when first frame is GPU-composited.
  {
    ReleaseFenceManager manager(dispatcher());
    std::vector<zx::event> release_fences = utils::CreateEventArray(2);
    zx::event render_finished_fence = utils::CreateEvent();

    bool callback_invoked = false;
    manager.OnGpuCompositedFrame(
        /*frame_number*/ 1, utils::CopyEvent(render_finished_fence),
        utils::CopyEventArray(release_fences),
        [&callback_invoked](scheduling::FrameRenderer::Timestamps) { callback_invoked = true; });

    for (auto& fence : release_fences) {
      EXPECT_TRUE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
    EXPECT_FALSE(utils::IsEventSignalled(render_finished_fence, ZX_EVENT_SIGNALED));
    EXPECT_FALSE(callback_invoked);
  }

  // Same thing, except with a direct-scanout frame.
  {
    ReleaseFenceManager manager(dispatcher());
    std::vector<zx::event> release_fences = utils::CreateEventArray(2);

    bool callback_invoked = false;
    manager.OnDirectScanoutFrame(
        /*frame_number*/ 1, utils::CopyEventArray(release_fences),
        [&callback_invoked](scheduling::FrameRenderer::Timestamps) { callback_invoked = true; });

    for (auto& fence : release_fences) {
      EXPECT_TRUE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
    EXPECT_FALSE(callback_invoked);
  }
}

TEST_F(ReleaseFenceManagerTest, SignalingWhenPreviousFrameWasGpuComposited) {
  // For the purposes of this test, it doesn't matter whether the second frame is GPU-composited or
  // direct-scanout.  Test both variants.
  for (auto& second_frame_is_gpu_composited : std::array<bool, 2>{true, false}) {
    ReleaseFenceManager manager(dispatcher());

    zx::event render_finished_fence = utils::CreateEvent();
    manager.OnGpuCompositedFrame(
        /*frame_number*/ 1, utils::CopyEvent(render_finished_fence), {},
        [](scheduling::FrameRenderer::Timestamps) {});

    // These fences will be passed along with the second frame, and signaled when the first frame is
    // finished rendering.
    std::vector<zx::event> release_fences = utils::CreateEventArray(2);

    if (second_frame_is_gpu_composited) {
      manager.OnGpuCompositedFrame(
          /*frame_number*/ 2, utils::CreateEvent(), utils::CopyEventArray(release_fences),
          [](scheduling::FrameRenderer::Timestamps) {});
    } else {
      manager.OnDirectScanoutFrame(
          /*frame_number*/ 2, utils::CopyEventArray(release_fences),
          [](scheduling::FrameRenderer::Timestamps) {});
    }

    // The fences provided with the second frame are not signaled until the first frame
    // is finished rendering.
    for (auto& fence : release_fences) {
      EXPECT_FALSE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
    render_finished_fence.signal(0u, ZX_EVENT_SIGNALED);
    RunLoopUntilIdle();
    for (auto& fence : release_fences) {
      EXPECT_TRUE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
  }
}

TEST_F(ReleaseFenceManagerTest, SignalingWhenPreviousFrameWasDirectScanout) {
  // For the purposes of this test, it doesn't matter whether the second frame is GPU-composited or
  // direct-scanout.  Test both variants.
  for (auto& second_frame_is_gpu_composited : std::array<bool, 2>{true, false}) {
    ReleaseFenceManager manager(dispatcher());

    manager.OnDirectScanoutFrame(
        /*frame_number*/ 1, {}, [](scheduling::FrameRenderer::Timestamps) {});

    // These fences will be passed along with the second frame, and signaled when the second frame
    // is displayed on screen (as evidenced by receiving an OnVsync()).
    std::vector<zx::event> release_fences = utils::CreateEventArray(2);

    if (second_frame_is_gpu_composited) {
      zx::event render_finished_fence = utils::CreateEvent();
      manager.OnGpuCompositedFrame(/*frame_number*/ 2, utils::CopyEvent(render_finished_fence),
                                   utils::CopyEventArray(release_fences),
                                   [](scheduling::FrameRenderer::Timestamps) {});

      // Finishing rendering doesn't signal the release fences, because the frame has not been
      // displayed yet.
      render_finished_fence.signal(0u, ZX_EVENT_SIGNALED);
      RunLoopUntilIdle();
      for (auto& fence : release_fences) {
        EXPECT_FALSE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
      }
    } else {
      manager.OnDirectScanoutFrame(
          /*frame_number*/ 2, utils::CopyEventArray(release_fences),
          [](scheduling::FrameRenderer::Timestamps) {});
    }

    // The fences are signaled when the second frame is displayed, not the first.
    manager.OnVsync(/*frame_number*/ 1, zx::time(1));
    for (auto& fence : release_fences) {
      EXPECT_FALSE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
    manager.OnVsync(/*frame_number*/ 2, zx::time(1));
    for (auto& fence : release_fences) {
      EXPECT_TRUE(utils::IsEventSignalled(fence, ZX_EVENT_SIGNALED));
    }
  }
}

TEST_F(ReleaseFenceManagerTest, FramePresentedCallbackForGpuCompositedFrame) {
  // Test common case, where render_finished_fence is signaled before the OnVsync() is received.
  {
    ReleaseFenceManager manager(dispatcher());
    zx::event render_finished_fence = utils::CreateEvent();

    bool callback_invoked = false;
    scheduling::FrameRenderer::Timestamps callback_timestamps;
    manager.OnGpuCompositedFrame(
        /*frame_number*/ 1, utils::CopyEvent(render_finished_fence), {},
        [&](scheduling::FrameRenderer::Timestamps timestamps) {
          callback_invoked = true;
          callback_timestamps = timestamps;
        });

    const zx::time pre_signal_time(zx_clock_get_monotonic());
    render_finished_fence.signal(0u, ZX_EVENT_SIGNALED);
    RunLoopUntilIdle();
    EXPECT_FALSE(callback_invoked);

    const zx::time vsync_time(zx_clock_get_monotonic());
    manager.OnVsync(/*frame_number*/ 1, vsync_time);
    EXPECT_TRUE(callback_invoked);
    EXPECT_GE(callback_timestamps.render_done_time, pre_signal_time);
    EXPECT_LE(callback_timestamps.render_done_time, vsync_time);
    EXPECT_EQ(callback_timestamps.actual_presentation_time, vsync_time);
  }

  // Test rare edge case, where render_finished_fence is signaled before the OnVsync() is received,
  // but we don't process is until afterward (unclear whether this will ever happen in practice).
  {
    ReleaseFenceManager manager(dispatcher());
    zx::event render_finished_fence = utils::CreateEvent();

    bool callback_invoked = false;
    scheduling::FrameRenderer::Timestamps callback_timestamps;
    manager.OnGpuCompositedFrame(
        /*frame_number*/ 1, utils::CopyEvent(render_finished_fence), {},
        [&](scheduling::FrameRenderer::Timestamps timestamps) {
          callback_invoked = true;
          callback_timestamps = timestamps;
        });

    const zx::time pre_signal_time(zx_clock_get_monotonic());
    render_finished_fence.signal(0u, ZX_EVENT_SIGNALED);

    const zx::time vsync_time(zx_clock_get_monotonic());
    manager.OnVsync(/*frame_number*/ 1, vsync_time);
    EXPECT_FALSE(callback_invoked);

    // This is where we process the event's signal.
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_invoked);
    EXPECT_GE(callback_timestamps.render_done_time, pre_signal_time);
    EXPECT_LE(callback_timestamps.render_done_time, vsync_time);
    EXPECT_EQ(callback_timestamps.actual_presentation_time, vsync_time);
  }
}

TEST_F(ReleaseFenceManagerTest, FramePresentedCallbackForDirectScanoutFrame) {
  ReleaseFenceManager manager(dispatcher());

  const zx::time kFrameStartTime(10'000'000);
  const zx::time kVsyncTime(12'000'000);
  RunLoopUntil(kFrameStartTime);

  bool callback_invoked = false;
  scheduling::FrameRenderer::Timestamps callback_timestamps;
  manager.OnDirectScanoutFrame(
      /*frame_number*/ 1, {}, [&](scheduling::FrameRenderer::Timestamps timestamps) {
        callback_invoked = true;
        callback_timestamps = timestamps;
      });

  manager.OnVsync(/*frame_number*/ 1, kVsyncTime);
  EXPECT_TRUE(callback_invoked);
  // TODO(fxbug.dev/74455): what should the render_done_time be?
  EXPECT_EQ(callback_timestamps.render_done_time, kFrameStartTime);
  EXPECT_EQ(callback_timestamps.actual_presentation_time, kVsyncTime);
}

TEST_F(ReleaseFenceManagerTest, OutOfOrderRenderFinished) {
  ReleaseFenceManager manager(dispatcher());

  bool callback_invoked1 = false;
  bool callback_invoked2 = false;
  bool callback_invoked3 = false;
  bool callback_invoked4 = false;
  scheduling::FrameRenderer::Timestamps callback_timestamps1;
  scheduling::FrameRenderer::Timestamps callback_timestamps2;
  scheduling::FrameRenderer::Timestamps callback_timestamps3;
  scheduling::FrameRenderer::Timestamps callback_timestamps4;
  zx::event render_finished_fence2 = utils::CreateEvent();
  zx::event render_finished_fence4 = utils::CreateEvent();

  manager.OnDirectScanoutFrame(
      /*frame_number*/ 1, {}, [&](scheduling::FrameRenderer::Timestamps timestamps) {
        callback_invoked1 = true;
        callback_timestamps1 = timestamps;
        EXPECT_FALSE(callback_invoked2);
        EXPECT_FALSE(callback_invoked3);
        EXPECT_FALSE(callback_invoked4);
      });
  EXPECT_EQ(manager.frame_record_count(), 1u);

  manager.OnGpuCompositedFrame(
      /*frame_number*/ 2, utils::CopyEvent(render_finished_fence2), {},
      [&](scheduling::FrameRenderer::Timestamps timestamps) {
        callback_invoked2 = true;
        callback_timestamps2 = timestamps;
        EXPECT_TRUE(callback_invoked1);
        EXPECT_FALSE(callback_invoked3);
        EXPECT_FALSE(callback_invoked4);
      });
  EXPECT_EQ(manager.frame_record_count(), 2u);

  manager.OnDirectScanoutFrame(
      /*frame_number*/ 3, {}, [&](scheduling::FrameRenderer::Timestamps timestamps) {
        callback_invoked3 = true;
        callback_timestamps3 = timestamps;
        EXPECT_TRUE(callback_invoked1);
        EXPECT_TRUE(callback_invoked2);
        EXPECT_FALSE(callback_invoked4);
      });
  EXPECT_EQ(manager.frame_record_count(), 3u);

  manager.OnGpuCompositedFrame(
      /*frame_number*/ 4, utils::CopyEvent(render_finished_fence4), {},
      [&](scheduling::FrameRenderer::Timestamps timestamps) {
        callback_invoked4 = true;
        callback_timestamps4 = timestamps;
        EXPECT_TRUE(callback_invoked1);
        EXPECT_TRUE(callback_invoked2);
        EXPECT_TRUE(callback_invoked3);
      });
  EXPECT_EQ(manager.frame_record_count(), 4u);

  EXPECT_FALSE(callback_invoked1);
  EXPECT_FALSE(callback_invoked2);
  EXPECT_FALSE(callback_invoked3);
  EXPECT_FALSE(callback_invoked4);

  // In this scenario, for some reason frame 4's rendering completes before frame 2's.  Although
  // this is unlikely, it's good to have this edge case covered in a reasonable way.  A more likely
  // scenario is that a direct-scanout frame (such as frame 3) is presented before the previous
  // GPU-composited frame is finished rendering; this scenario is also covered here.

  const zx::time pre_signal_time4(zx_clock_get_monotonic());
  render_finished_fence4.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_invoked4);
  const zx::time vsync_time(zx_clock_get_monotonic());
  manager.OnVsync(/*frame_number*/ 4, vsync_time);

  // Even though frame 4 has been presented, we can only invoke the first callback.  This is because
  // of scheduling::FrameRenderer's requirement that: "Frames must be rendered in the order they are
  // requested, and callbacks must be triggered in the same order."
  EXPECT_TRUE(callback_invoked1);
  EXPECT_FALSE(callback_invoked2);
  EXPECT_FALSE(callback_invoked3);
  EXPECT_FALSE(callback_invoked4);
  EXPECT_EQ(callback_timestamps1.actual_presentation_time, vsync_time);
  EXPECT_EQ(manager.frame_record_count(), 3u);

  // Once frame 2's render-finished fence has been signaled, this "unlocks" the rest of the frames.
  const zx::time pre_signal_time2(zx_clock_get_monotonic());
  render_finished_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();

  EXPECT_TRUE(callback_invoked2);
  EXPECT_TRUE(callback_invoked3);
  EXPECT_TRUE(callback_invoked4);
  EXPECT_EQ(callback_timestamps2.actual_presentation_time, vsync_time);
  EXPECT_EQ(callback_timestamps3.actual_presentation_time, vsync_time);
  EXPECT_EQ(callback_timestamps4.actual_presentation_time, vsync_time);

  // Even though all frame callbacks have been invoked, the frame record for the last frame is kept
  // around, because its type (GPU-composited vs. direct-scanout) affects how the *next* frame's
  // release fences are handled.
  EXPECT_EQ(manager.frame_record_count(), 1u);

  // Adding an additional frame results in the old frame-record being erased, and a new one added.
  manager.OnDirectScanoutFrame(
      /*frame_number*/ 5, {}, [&](scheduling::FrameRenderer::Timestamps) {});
  EXPECT_EQ(manager.frame_record_count(), 1u);
}

TEST_F(ReleaseFenceManagerTest, ImmediateErasure) {
  // Frame is erased immediately when a subsequent frame is added, after the first frame already has
  // its callback invoked (we don't test the callback explicitly here; this is done in other tests).
  {
    ReleaseFenceManager manager(dispatcher());

    // First frame can't be erased even after presented.
    manager.OnDirectScanoutFrame(/*frame_number*/ 1, {},
                                 [](scheduling::FrameRenderer::Timestamps) {});
    manager.OnVsync(/*frame_number*/ 1, zx::time(100));
    EXPECT_EQ(manager.frame_record_count(), 1u);

    // Adding the next frame causes the first to be erased.
    zx::event render_finished_fence = utils::CreateEvent();
    manager.OnGpuCompositedFrame(
        /*frame_number*/ 2, utils::CopyEvent(render_finished_fence), {},
        [](scheduling::FrameRenderer::Timestamps) {});
    EXPECT_EQ(manager.frame_record_count(), 1u);

    // Second frame can't be erased even after render-finished and presented.
    render_finished_fence.signal(0u, ZX_EVENT_SIGNALED);
    RunLoopUntilIdle();
    manager.OnVsync(/*frame_number*/ 2, zx::time(200));
    EXPECT_EQ(manager.frame_record_count(), 1u);

    // Adding the next frame causes the second to be erased.
    manager.OnDirectScanoutFrame(/*frame_number*/ 3, {},
                                 [](scheduling::FrameRenderer::Timestamps) {});
    EXPECT_EQ(manager.frame_record_count(), 1u);
  }

  // GPU-composited frame is erased immediately when there is already a subsequent frame, rendering
  // has finished, and it has been presented (the last 2 in either order).
  {
    ReleaseFenceManager manager(dispatcher());
    zx::event render_finished_fence1 = utils::CreateEvent();
    zx::event render_finished_fence2 = utils::CreateEvent();

    manager.OnGpuCompositedFrame(
        /*frame_number*/ 1, utils::CopyEvent(render_finished_fence1), {},
        [](scheduling::FrameRenderer::Timestamps) {});

    manager.OnGpuCompositedFrame(
        /*frame_number*/ 2, utils::CopyEvent(render_finished_fence2), {},
        [](scheduling::FrameRenderer::Timestamps) {});

    // First frame has fence signaled before OnVsync().  The other way works too, as we see below.
    render_finished_fence1.signal(0u, ZX_EVENT_SIGNALED);
    RunLoopUntilIdle();
    EXPECT_EQ(manager.frame_record_count(), 2u);
    manager.OnVsync(/*frame_number*/ 1, zx::time(100));
    EXPECT_EQ(manager.frame_record_count(), 1u);

    // Add a third frame, so the second can be erased immediately after its callback is invoked.
    manager.OnDirectScanoutFrame(/*frame_number*/ 3, {},
                                 [](scheduling::FrameRenderer::Timestamps) {});

    // Second frame has OnVsync() before fence signal is received.
    render_finished_fence2.signal(0u, ZX_EVENT_SIGNALED);
    manager.OnVsync(/*frame_number*/ 2, zx::time(200));
    EXPECT_EQ(manager.frame_record_count(), 2u);
    RunLoopUntilIdle();  // handle the signaling of |render_finished_fence2|
    EXPECT_EQ(manager.frame_record_count(), 1u);
  }

  // Direct-scanout frame is erased immediately when there is already a subsequent frame, as soon as
  // its callback is invoked.
  {
    ReleaseFenceManager manager(dispatcher());

    manager.OnDirectScanoutFrame(/*frame_number*/ 1, {},
                                 [](scheduling::FrameRenderer::Timestamps) {});
    manager.OnDirectScanoutFrame(/*frame_number*/ 2, {},
                                 [](scheduling::FrameRenderer::Timestamps) {});

    EXPECT_EQ(manager.frame_record_count(), 2u);
    manager.OnVsync(/*frame_number*/ 1, zx::time(100));
    EXPECT_EQ(manager.frame_record_count(), 1u);
  }
}

TEST_F(ReleaseFenceManagerTest, RepeatedOnVsyncFrameNumbers) {
  ReleaseFenceManager manager(dispatcher());

  uint64_t callback_count1 = 0;
  manager.OnDirectScanoutFrame(/*frame_number*/ 1, {},
                               [&](scheduling::FrameRenderer::Timestamps) { ++callback_count1; });

  manager.OnVsync(/*frame_number*/ 1, zx::time(100));
  EXPECT_EQ(callback_count1, 1u);
  manager.OnVsync(/*frame_number*/ 1, zx::time(200));
  manager.OnVsync(/*frame_number*/ 1, zx::time(300));
  manager.OnVsync(/*frame_number*/ 1, zx::time(400));
  manager.OnVsync(/*frame_number*/ 1, zx::time(500));
  EXPECT_EQ(callback_count1, 1u);

  // Register another frame, but have more Vsyncs for the first frame arrive before the second is
  // presented.
  uint64_t callback_count2 = 0;
  manager.OnDirectScanoutFrame(/*frame_number*/ 2, {},
                               [&](scheduling::FrameRenderer::Timestamps) { ++callback_count2; });

  manager.OnVsync(/*frame_number*/ 1, zx::time(600));
  EXPECT_EQ(callback_count1, 1u);
  EXPECT_EQ(callback_count2, 0u);

  manager.OnVsync(/*frame_number*/ 2, zx::time(700));
  EXPECT_EQ(callback_count1, 1u);
  EXPECT_EQ(callback_count2, 1u);
}

}  // namespace flatland::test
