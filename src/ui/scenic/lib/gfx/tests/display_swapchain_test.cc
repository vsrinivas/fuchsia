// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <zircon/syscalls.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/lib/display/get_hardware_display_controller.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/swapchain/frame_timings.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using escher::ImageFactoryAdapter;
using escher::VulkanDeviceQueues;
using escher::VulkanDeviceQueuesPtr;
using escher::VulkanInstance;
using scheduling::test::MockFrameScheduler;

using Fixture = gtest::RealLoopFixture;

namespace {
constexpr zx::duration kVsyncTimeout = zx::msec(1000);
}  // namespace

class DisplaySwapchainTest : public Fixture {
 public:
  std::unique_ptr<DisplaySwapchain> CreateSwapchain(display::Display* display) {
    return std::make_unique<DisplaySwapchain>(
        sysmem(), display_manager()->default_display_controller(),
        display_manager()->default_display_controller_listener(), 2, display, escher());
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

    auto hdc_promise = ui_display::GetHardwareDisplayController();
    executor_->schedule_task(
        hdc_promise.then([this](fpromise::result<ui_display::DisplayControllerHandles>& handles) {
          display_manager_->BindDefaultDisplayController(std::move(handles.value().controller));
        }));

    RunLoopUntil([this] { return display_manager_->default_display() != nullptr; });
  }

  // |testing::Test|
  void TearDown() override {
    if (VK_TESTS_SUPPRESSED()) {
      return;
    }
    image_factory_.reset();
    escher_.reset();
    sysmem_.reset();
    executor_.reset();
    display_manager_.reset();
    session_.reset();
    error_reporter_.reset();
    event_reporter_.reset();
    Fixture::TearDown();
  }

  void SetUpEscherAndSession(escher::VulkanDeviceQueuesPtr vulkan_device) {
    escher_ = std::make_unique<escher::Escher>(vulkan_device);
    image_factory_ = std::make_unique<ImageFactoryAdapter>(escher_->gpu_allocator(),
                                                           escher_->resource_recycler());
    frame_scheduler_ = std::make_shared<MockFrameScheduler>();
    auto session_context = SessionContext{.vk_device = escher_->vk_device(),
                                          .escher = escher_.get(),
                                          .escher_resource_recycler = escher_->resource_recycler(),
                                          .escher_image_factory = image_factory_.get(),
                                          .scene_graph = SceneGraphWeakPtr()};
    error_reporter_ = std::make_shared<TestErrorReporter>();
    event_reporter_ = std::make_shared<TestEventReporter>();
    session_ = std::make_unique<Session>(1, session_context, event_reporter_, error_reporter_);
  }

  escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory) {
    VulkanInstance::Params instance_params(
        {{"VK_LAYER_KHRONOS_validation"},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
         false});

    auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
    // This extension is necessary to support exporting Vulkan memory to a VMO.
    VulkanDeviceQueues::Params device_params(
        {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
          VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME, VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
          VK_FUCHSIA_BUFFER_COLLECTION_EXTENSION_NAME},
         {},
         vk::SurfaceKHR()});
    if (use_protected_memory)
      device_params.flags = VulkanDeviceQueues::Params::kAllowProtectedMemory;
    auto queues = VulkanDeviceQueues::New(vulkan_instance, device_params);
    if (use_protected_memory && !queues->caps().allow_protected_memory) {
      return nullptr;
    }
    return queues;
  }

  void DrawAndPresentFrame(DisplaySwapchain* swapchain, const std::shared_ptr<FrameTimings>& timing,
                           size_t swapchain_index, Layer& layer) {
    swapchain->DrawAndPresentFrame(
        timing, swapchain_index, layer,
        [this, timing](const escher::ImagePtr& out, Layer& layer, const escher::SemaphorePtr& wait,
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

  std::shared_ptr<FrameTimings> MakeTimings(uint64_t frame_number) {
    FX_CHECK(frame_scheduler_);
    return std::make_shared<FrameTimings>(
        frame_number, [this](const FrameTimings& timings) { ++frame_presented_call_count_; });
  }

  BufferPool* Framebuffers(DisplaySwapchain* swapchain, bool use_protected_memory) const {
    return use_protected_memory ? &swapchain->protected_swapchain_buffers_
                                : &swapchain->swapchain_buffers_;
  }

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  std::unique_ptr<async::Executor> executor_;
  display::DisplayManager* display_manager() { return display_manager_.get(); }
  Session* session() { return session_.get(); }
  display::Display* display() { return display_manager()->default_display(); }
  std::shared_ptr<MockFrameScheduler> scheduler() { return frame_scheduler_; }
  uint32_t frame_presented_call_count() { return frame_presented_call_count_; }

 private:
  uint32_t frame_presented_call_count_ = 0;

  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<Session> session_;
  std::shared_ptr<MockFrameScheduler> frame_scheduler_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::shared_ptr<TestErrorReporter> error_reporter_;
  std::shared_ptr<TestEventReporter> event_reporter_;
};

VK_TEST_F(DisplaySwapchainTest, RenderStress) {
  SetUpEscherAndSession(CreateVulkanDeviceQueues(/*use_protected_memory*/ false));
  auto swapchain = CreateSwapchain(display());

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);

  constexpr size_t kNumFrames = 100;
  std::array<std::shared_ptr<FrameTimings>, kNumFrames> timings;
  for (size_t i = 0; i < kNumFrames; ++i) {
    zx::time now(async_now(dispatcher()));
    timings[i] = MakeTimings(i);
    timings[i]->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), timings[i], 0, *layer.get());
    EXPECT_TRUE(
        RunLoopWithTimeoutOrUntil([t = timings[i]]() { return t->finalized(); }, kVsyncTimeout));
  }
  RunLoopUntilIdle();
  // Last frame is left up on the display, so look for presentation.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this]() { return frame_presented_call_count() == kNumFrames; }, kVsyncTimeout));
}

VK_TEST_F(DisplaySwapchainTest, RenderProtectedStress) {
  // Check if we can create escher that support protected memory.
  auto vulkan_device = CreateVulkanDeviceQueues(/*use_protected_memory*/ true);
  if (!vulkan_device) {
    GTEST_SKIP();
  }
  SetUpEscherAndSession(vulkan_device);

  auto swapchain = CreateSwapchain(display());
  swapchain->SetUseProtectedMemory(true);

  auto layer = fxl::MakeRefCounted<Layer>(session(), session()->id(), 0);

  constexpr size_t kNumFrames = 100;
  std::array<std::shared_ptr<FrameTimings>, kNumFrames> timings;
  for (size_t i = 0; i < kNumFrames; ++i) {
    zx::time now(async_now(dispatcher()));
    timings[i] = MakeTimings(i);
    timings[i]->RegisterSwapchains(1);
    DrawAndPresentFrame(swapchain.get(), timings[i], 0, *layer.get());
    EXPECT_TRUE(
        RunLoopWithTimeoutOrUntil([t = timings[i]]() { return t->finalized(); }, kVsyncTimeout));
  }
  RunLoopUntilIdle();
  // Last frame is left up on the display, so look for presentation.
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [this]() { return frame_presented_call_count() == kNumFrames; }, kVsyncTimeout));
}

VK_TEST_F(DisplaySwapchainTest, InitializesFramebuffers) {
  SetUpEscherAndSession(CreateVulkanDeviceQueues(/*use_protected_memory*/ false));
  auto swapchain = CreateSwapchain(display());

  BufferPool* buffer_pool = Framebuffers(swapchain.get(), /*use_protected_memory=*/false);
  EXPECT_EQ(2u, buffer_pool->size());
  zx::vmo vmo = ExportMemoryAsVmo(escher(), buffer_pool->GetUnused()->device_memory);
  EXPECT_EQ(0u, fsl::GetObjectName(vmo.get()).find("Display"));
}

VK_TEST_F(DisplaySwapchainTest, InitializesProtectedFramebuffers) {
  // Check if we can create escher that support protected memory.
  auto vulkan_device = CreateVulkanDeviceQueues(/*use_protected_memory*/ true);
  if (!vulkan_device) {
    GTEST_SKIP();
  }
  SetUpEscherAndSession(vulkan_device);

  auto swapchain = CreateSwapchain(display());

  BufferPool* buffer_pool = Framebuffers(swapchain.get(), /*use_protected_memory=*/true);
  EXPECT_EQ(2u, buffer_pool->size());
  zx::vmo vmo = ExportMemoryAsVmo(escher(), buffer_pool->GetUnused()->device_memory);
  EXPECT_EQ(0u, fsl::GetObjectName(vmo.get()).find("Display-Protected"));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
