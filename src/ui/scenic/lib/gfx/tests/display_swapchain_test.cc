// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using escher::ImageFactoryAdapter;
using escher::ReleaseFenceSignaller;
using escher::VulkanDeviceQueues;
using escher::VulkanDeviceQueuesPtr;
using escher::VulkanInstance;
using scheduling::FrameTimings;
using scheduling::test::MockFrameScheduler;

using Fixture = gtest::RealLoopFixture;

class DisplaySwapchainTest : public Fixture {
 public:
  std::unique_ptr<DisplaySwapchain> CreateSwapchain(display::Display* display) {
    return std::make_unique<DisplaySwapchain>(
        sysmem(), display_manager()->default_display_controller(),
        display_manager()->default_display_controller_listener(), display, escher());
  }

  void DrawAndPresentFrame(DisplaySwapchain* swapchain, fxl::WeakPtr<FrameTimings> timing,
                           size_t swapchain_index, const HardwareLayerAssignment& hla,
                           zx::event frame_retired) {
    swapchain->DrawAndPresentFrame(
        timing, swapchain_index, hla, std::move(frame_retired),
        [this, timing](zx::time present_time, const escher::ImagePtr& out,
                       const HardwareLayerAssignment::Item& hla, const escher::SemaphorePtr& wait,
                       const escher::SemaphorePtr& signal) {
          auto device = escher()->device();
          zx_signals_t tmp;
          auto wait_event = GetEventForSemaphore(device, wait);
          EXPECT_EQ(wait_event.wait_one(ZX_EVENT_SIGNALED, zx::time(ZX_TIME_INFINITE), &tmp),
                    ZX_OK);
          EXPECT_EQ(wait_event.signal(/*clear_mask=*/ZX_EVENT_SIGNALED, 0), ZX_OK);
          EXPECT_EQ(GetEventForSemaphore(device, signal).signal(0, ZX_EVENT_SIGNALED), ZX_OK);
        });
  }

  void OnVsync(DisplaySwapchain* swapchain, zx::time timestamp,
               const std::vector<uint64_t>& image_ids) {
    swapchain->OnVsync(display()->display_id(), timestamp.get(), image_ids);
  }

  const std::vector<DisplaySwapchain::Framebuffer>& Framebuffers(
      DisplaySwapchain* swapchain) const {
    return swapchain->swapchain_buffers_;
  }

  std::unique_ptr<FrameTimings> MakeTimings(uint64_t frame_number, zx::time present, zx::time latch,
                                            zx::time started) {
    FXL_CHECK(frame_scheduler_);
    return std::make_unique<FrameTimings>(
        frame_number, present, latch, started,
        fit::bind_member(frame_scheduler_.get(), &MockFrameScheduler::OnFrameRendered),
        fit::bind_member(frame_scheduler_.get(), &MockFrameScheduler::OnFramePresented));
  }

  // |testing::Test|
  void SetUp() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    Fixture::SetUp();
    async_set_default_dispatcher(dispatcher());
    sysmem_ = std::make_unique<Sysmem>();
    display_manager_ = std::make_unique<display::DisplayManager>();
    auto vulkan_device = CreateVulkanDeviceQueues();
    escher_ = std::make_unique<escher::Escher>(vulkan_device);
    release_fence_signaller_ =
        std::make_unique<ReleaseFenceSignaller>(escher_->command_buffer_sequencer());
    image_factory_ = std::make_unique<ImageFactoryAdapter>(escher_->gpu_allocator(),
                                                           escher_->resource_recycler());
    frame_scheduler_ = std::make_shared<MockFrameScheduler>();
    auto session_context = SessionContext{escher_->vk_device(),
                                          escher_.get(),
                                          escher_->resource_recycler(),
                                          image_factory_.get(),
                                          nullptr,
                                          release_fence_signaller_.get(),
                                          frame_scheduler_,
                                          SceneGraphWeakPtr(),
                                          nullptr};
    error_reporter_ = std::make_shared<TestErrorReporter>();
    event_reporter_ = std::make_shared<TestEventReporter>();
    session_ = std::make_unique<Session>(1, session_context, event_reporter_, error_reporter_);
    display_manager_->WaitForDefaultDisplayController([] {});
    RunLoopUntil([this] { return display_manager_->default_display() != nullptr; });
  }

  // |testing::Test|
  void TearDown() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    image_factory_.reset();
    release_fence_signaller_.reset();
    escher_.reset();
    sysmem_.reset();
    display_manager_.reset();
    session_.reset();
    error_reporter_.reset();
    event_reporter_.reset();
    Fixture::TearDown();
  }

  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues() {
    VulkanInstance::Params instance_params(
        {{"VK_LAYER_LUNARG_standard_validation"},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
         false});
    auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
    // This extension is necessary to support exporting Vulkan memory to a VMO.
    return VulkanDeviceQueues::New(
        vulkan_instance,
        {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
          VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME, VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
          VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME},
         {},
         vk::SurfaceKHR()});
  }

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  display::DisplayManager* display_manager() { return display_manager_.get(); }
  Session* session() { return session_.get(); }
  display::Display* display() { return display_manager()->default_display(); }
  std::shared_ptr<scheduling::test::MockFrameScheduler> scheduler() { return frame_scheduler_; }

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<Session> session_;
  std::shared_ptr<MockFrameScheduler> frame_scheduler_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
  std::shared_ptr<TestErrorReporter> error_reporter_;
  std::shared_ptr<TestEventReporter> event_reporter_;
};

class TestFrame {
 public:
  void Init(async_dispatcher_t* dispatcher) {
    ASSERT_EQ(zx::event::create(0, &retired), ZX_OK);
    zx::event dup;
    ASSERT_EQ(retired.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);
    retired_wait = std::make_unique<async::Wait>(
        dup.release(), ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_TIMESTAMP,
        [](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {});
    retired_wait->Begin(dispatcher);
  }

  zx::event retired;
  std::unique_ptr<async::Wait> retired_wait;
  std::unique_ptr<FrameTimings> timings;
};

// This test runs against the actual display's retirement and vsync.
VK_TEST_F(DisplaySwapchainTest, RenderStress) {
  auto swapchain = CreateSwapchain(display());

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);
  HardwareLayerAssignment hla({{{0, {layer.get()}}}, swapchain.get()});

  constexpr size_t kNumFrames = 100;
  constexpr size_t kMaxOutstanding = DisplaySwapchain::kSwapchainImageCount;
  TestFrame frames[kNumFrames];
  for (auto& frame : frames) {
    frame.Init(dispatcher());
  }

  for (size_t i = 0; i < kNumFrames; ++i) {
    zx::time now(async_now(dispatcher()));
    auto& frame = frames[i];
    RunLoopUntil([&frames, i] {
      return i < kMaxOutstanding || !frames[i - kMaxOutstanding].retired_wait->is_pending();
    });
    frame.timings = MakeTimings(i, now + zx::msec(15), now + zx::msec(10), now);
    frame.timings->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), frame.timings->GetWeakPtr(), 0, hla,
                        std::move(frame.retired));
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this] { return scheduler()->frame_rendered_call_count() == kNumFrames; }));
  // Last frame is left up on the display, so look for presentation.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this] { return scheduler()->frame_presented_call_count() == kNumFrames; },
      /*timeout=*/zx::msec(kNumFrames * 100)));
  EXPECT_EQ(scheduler()->frame_rendered_call_count(), kNumFrames);
  EXPECT_EQ(scheduler()->frame_presented_call_count(), kNumFrames);
  RunLoopUntilIdle();
}

// TODO(fxb/24720): Use a fake display to add more interesting ordering tests. For now this test
// runs against the actual display's retirment and vsync ordering, and we just ensure that the
// reported frame timings obey simple rules.
VK_TEST_F(DisplaySwapchainTest, MultipleRendersBeforeVsync_PresentInOrder) {
  auto swapchain = CreateSwapchain(display());
  (*display_manager()->default_display_controller())->EnableVsync(true);
  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);
  HardwareLayerAssignment hla({{{0, {layer.get()}}}, swapchain.get()});

  constexpr size_t kNumFrames = 30;
  // Limit to two buffers to ensure we don't render too quickly for the display.
  constexpr int kMaxOutstanding = 2;
  TestFrame frames[kNumFrames];
  for (auto& frame : frames) {
    frame.Init(dispatcher());
  }

  zx::time now(async_now(dispatcher()));
  for (size_t i = 0; i < kNumFrames; i++) {
    RunLoopUntil([&frames, i] {
      return i < kMaxOutstanding || !frames[i - kMaxOutstanding].retired_wait->is_pending();
    });
    zx::time latch = now + zx::msec(i * 15 + 10);
    zx::time target_present = now + zx::msec(i * 15 + 15);
    frames[i].timings = MakeTimings(i, target_present, latch, now);
    frames[i].timings->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), frames[i].timings->GetWeakPtr(), 0, hla,
                        std::move(frames[i].retired));
  }

  // Last frame is left up on the display
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&frames] { return !frames[kNumFrames - 2].retired_wait->is_pending(); }));

  // Verify that frames are displayed in order or dropped.
  auto last_time = frames[0].timings->GetTimestamps().actual_presentation_time;
  size_t dropped = 0;
  for (size_t i = 1; i < kNumFrames; ++i) {
    EXPECT_TRUE(frames[i].timings->finalized());
    EXPECT_FALSE(frames[i].timings->FrameWasDropped());
    if (!frames[i].timings->FrameWasDropped()) {
      EXPECT_GT(frames[i].timings->GetTimestamps().actual_presentation_time, last_time);
      last_time = frames[i].timings->GetTimestamps().actual_presentation_time;
    }
  }
  EXPECT_EQ(dropped, 0u);
  RunLoopUntilIdle();
}

VK_TEST_F(DisplaySwapchainTest, MultipleVsyncsBeforeRender_PresentFirstTime) {
  auto swapchain = CreateSwapchain(display());
  // Swallow the actual vsync callbacks, we'll drive this as part of the test instead.
  display_manager()->default_display_controller_listener()->SetVsyncCallback(
      [](uint64_t, uint64_t, const std::vector<uint64_t>& image_ids) {});
  (*display_manager()->default_display_controller())->EnableVsync(true);

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);
  HardwareLayerAssignment hla({{{0, {layer.get()}}}, swapchain.get()});

  constexpr size_t kNumFrames = 5;
  constexpr int kMaxOutstanding = DisplaySwapchain::kSwapchainImageCount;
  TestFrame frames[kNumFrames];
  for (auto& frame : frames) {
    frame.Init(dispatcher());
  }

  zx::time now(async_now(dispatcher()));
  // Render the first frame.
  {
    constexpr size_t i = 0;
    zx::time latch = now + zx::msec(10);
    zx::time target_present = now + zx::msec(15);
    frames[i].timings = MakeTimings(i, target_present, latch, now);
    frames[i].timings->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), frames[i].timings->GetWeakPtr(), 0, hla,
                        std::move(frames[i].retired));
  }
  RunLoopUntil([this] { return scheduler()->frame_rendered_call_count() == 1; });

  // Vsync a couple of times with the old image
  zx::time first_vsync = now;
  for (size_t i = 0; i < 5; i++) {
    EXPECT_EQ(scheduler()->frame_rendered_call_count(), 1UL);
    auto fb_id = Framebuffers(swapchain.get())[0].fb_id;
    OnVsync(swapchain.get(), now, {fb_id});
  }

  // Render the other frames.
  for (size_t i = 1; i < kNumFrames; i++) {
    RunLoopUntil([&frames, i] {
      return i < kMaxOutstanding || !frames[i - kMaxOutstanding].retired_wait->is_pending();
    });
    now = zx::time(async_now(dispatcher()));
    zx::time latch = now + zx::msec(10);
    zx::time target_present = now + zx::msec(15);
    frames[i].timings = MakeTimings(i, target_present, latch, now);
    frames[i].timings->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), frames[i].timings->GetWeakPtr(), 0, hla,
                        std::move(frames[i].retired));
    auto fb_id = Framebuffers(swapchain.get())[i % DisplaySwapchain::kSwapchainImageCount].fb_id;
    OnVsync(swapchain.get(), now, {fb_id});
  }
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this] { return scheduler()->frame_rendered_call_count() == kNumFrames; }));
  // Last frame is left up on the display
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this] { return scheduler()->frame_presented_call_count() == kNumFrames; }));
  EXPECT_EQ(scheduler()->frame_presented_call_count(), kNumFrames);
  EXPECT_TRUE(frames[0].timings->finalized());
  EXPECT_EQ(frames[0].timings->GetTimestamps().actual_presentation_time, first_vsync);
  RunLoopUntilIdle();
}

VK_TEST_F(DisplaySwapchainTest, InitializesFramebuffers) {
  auto swapchain = CreateSwapchain(display());
  EXPECT_EQ(3u, Framebuffers(swapchain.get()).size());
}
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
