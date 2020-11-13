// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/syscalls.h>

#include <gtest/gtest.h>

#include "src/ui/lib/display/get_hardware_display_controller.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
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

  // |testing::Test|
  void SetUp() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    gtest::RealLoopFixture::SetUp();

    async_set_default_dispatcher(dispatcher());
    executor_ = std::make_unique<async::Executor>(dispatcher());
    sysmem_ = std::make_unique<Sysmem>();
    display_manager_ = std::make_unique<display::DisplayManager>([]() {});

    auto vulkan_device = CreateVulkanDeviceQueues(/*use_protected_memory*/ false);
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
                                          release_fence_signaller_.get(),
                                          frame_scheduler_,
                                          SceneGraphWeakPtr(),
                                          nullptr};
    error_reporter_ = std::make_shared<TestErrorReporter>();
    event_reporter_ = std::make_shared<TestEventReporter>();
    session_ = std::make_unique<Session>(1, session_context, event_reporter_, error_reporter_);

    auto hdc_promise = ui_display::GetHardwareDisplayController();
    executor_->schedule_task(
        hdc_promise.then([this](fit::result<ui_display::DisplayControllerHandles>& handles) {
          display_manager_->BindDefaultDisplayController(std::move(handles.value().controller),
                                                         std::move(handles.value().dc_device));
        }));

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
    executor_.reset();
    display_manager_.reset();
    session_.reset();
    error_reporter_.reset();
    event_reporter_.reset();
    Fixture::TearDown();
  }

  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory) {
    VulkanInstance::Params instance_params(
        {{"VK_LAYER_KHRONOS_validation"},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
         false});

    auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
    // This extension is necessary to support exporting Vulkan memory to a VMO.
    auto queues = VulkanDeviceQueues::New(
        vulkan_instance,
        {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
          VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME, VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
          VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME},
         {},
         vk::SurfaceKHR()});
    if (use_protected_memory && !queues->caps().allow_protected_memory) {
      return nullptr;
    }
    return queues;
  }

  void DrawAndPresentFrame(DisplaySwapchain* swapchain, fxl::WeakPtr<FrameTimings> timing,
                           size_t swapchain_index, const HardwareLayerAssignment& hla) {
    swapchain->DrawAndPresentFrame(
        timing, swapchain_index, hla,
        [this, timing](zx::time present_time, const escher::ImagePtr& out,
                       const HardwareLayerAssignment::Item& hla, const escher::SemaphorePtr& wait,
                       const escher::SemaphorePtr& signal) {
          auto device = escher()->device();
          zx_signals_t tmp;
          if (wait) {
            EXPECT_EQ(GetEventForSemaphore(device, wait)
                          .wait_one(ZX_EVENT_SIGNALED, zx::time(ZX_TIME_INFINITE), &tmp),
                      ZX_OK);
          }
          if (signal) {
            EXPECT_EQ(GetEventForSemaphore(device, signal).signal(0, ZX_EVENT_SIGNALED), ZX_OK);
          }
        });
  }

  std::unique_ptr<FrameTimings> MakeTimings(uint64_t frame_number, zx::time present_time,
                                            zx::time latch_time, zx::time started_time) {
    FX_CHECK(frame_scheduler_);
    return std::make_unique<FrameTimings>(
        frame_number, present_time, latch_time, started_time,
        [this](const FrameTimings& timings) { ++frame_presented_call_count_; },
        [this](const FrameTimings& timings) { ++frame_rendered_call_count_; });
  }

  const BufferPool& Framebuffers(DisplaySwapchain* swapchain) const {
    return swapchain->swapchain_buffers_;
  }

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  std::unique_ptr<async::Executor> executor_;
  display::DisplayManager* display_manager() { return display_manager_.get(); }
  Session* session() { return session_.get(); }
  display::Display* display() { return display_manager()->default_display(); }
  std::shared_ptr<MockFrameScheduler> scheduler() { return frame_scheduler_; }
  uint32_t frame_presented_call_count() { return frame_presented_call_count_; }
  uint32_t frame_rendered_call_count() { return frame_rendered_call_count_; }

 private:
  uint32_t frame_presented_call_count_ = 0;
  uint32_t frame_rendered_call_count_ = 0;

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

VK_TEST_F(DisplaySwapchainTest, RenderStress) {
  auto swapchain = CreateSwapchain(display());

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);
  HardwareLayerAssignment hla({{{0, {layer.get()}}}, swapchain.get()});

  constexpr size_t kNumFrames = 100;
  std::array<std::unique_ptr<FrameTimings>, kNumFrames> timings;
  for (size_t i = 0; i < kNumFrames; ++i) {
    zx::time now(async_now(dispatcher()));
    timings[i] = MakeTimings(i, now + zx::msec(15), now + zx::msec(10), now);
    timings[i]->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), timings[i]->GetWeakPtr(), 0, hla);
    EXPECT_TRUE(RunLoopWithTimeoutOrUntil([t = timings[i].get()]() { return t->finalized(); },
                                          /*timeout=*/zx::msec(50)));
  }
  RunLoopUntilIdle();
  EXPECT_EQ(frame_rendered_call_count(), kNumFrames);
  // Last frame is left up on the display, so look for presentation.
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([this]() { return frame_presented_call_count() == kNumFrames; },
                                /*timeout=*/zx::msec(50)));
}

VK_TEST_F(DisplaySwapchainTest, RenderProtectedStress) {
  if (!CreateVulkanDeviceQueues(/*use_protected_memory=*/true)) {
    GTEST_SKIP();
  }
  auto swapchain = CreateSwapchain(display());
  swapchain->SetUseProtectedMemory(true);

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);
  HardwareLayerAssignment hla({{{0, {layer.get()}}}, swapchain.get()});

  constexpr size_t kNumFrames = 100;
  std::array<std::unique_ptr<FrameTimings>, kNumFrames> timings;
  for (size_t i = 0; i < kNumFrames; ++i) {
    zx::time now(async_now(dispatcher()));
    timings[i] = MakeTimings(i, now + zx::msec(15), now + zx::msec(10), now);
    timings[i]->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), timings[i]->GetWeakPtr(), 0, hla);
    EXPECT_TRUE(RunLoopWithTimeoutOrUntil([t = timings[i].get()]() { return t->finalized(); },
                                          /*timeout=*/zx::msec(50)));
  }
  RunLoopUntilIdle();
  EXPECT_EQ(frame_rendered_call_count(), kNumFrames);
  // Last frame is left up on the display, so look for presentation.
  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([this]() { return frame_presented_call_count() == kNumFrames; },
                                /*timeout=*/zx::msec(50)));
}

VK_TEST_F(DisplaySwapchainTest, InitializesFramebuffers) {
  auto swapchain = CreateSwapchain(display());
  EXPECT_EQ(3u, Framebuffers(swapchain.get()).size());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
