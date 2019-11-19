// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

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
#include "src/ui/scenic/lib/scheduling/frame_timings.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using escher::ImageFactoryAdapter;
using escher::ReleaseFenceSignaller;
using escher::VulkanDeviceQueues;
using escher::VulkanDeviceQueuesPtr;
using escher::VulkanInstance;
using scheduling::test::MockFrameScheduler;

using Fixture = gtest::RealLoopFixture;

class DisplaySwapchainTest : public Fixture {
 public:
  std::unique_ptr<DisplaySwapchain> CreateSwapchain(Display* display) {
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
    sysmem_ = std::make_unique<Sysmem>();
    display_manager_ = std::make_unique<DisplayManager>();

    auto vulkan_device = CreateVulkanDeviceQueues();
    escher_ = std::make_unique<escher::Escher>(vulkan_device);
    release_fence_signaller_ =
        std::make_unique<ReleaseFenceSignaller>(escher_->command_buffer_sequencer());
    image_factory_ = std::make_unique<ImageFactoryAdapter>(escher_->gpu_allocator(),
                                                           escher_->resource_recycler());
    auto session_context = SessionContext{escher_->vk_device(),
                                          escher_.get(),
                                          escher_->resource_recycler(),
                                          image_factory_.get(),
                                          nullptr,
                                          release_fence_signaller_.get(),
                                          frame_scheduler_,
                                          SceneGraphWeakPtr(),
                                          nullptr,
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

  const std::vector<DisplaySwapchain::Framebuffer>& Framebuffers(
      DisplaySwapchain* swapchain) const {
    return swapchain->swapchain_buffers_;
  }

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  DisplayManager* display_manager() { return display_manager_.get(); }
  Session* session() { return session_.get(); }
  Display* display() { return display_manager()->default_display(); }
  std::shared_ptr<MockFrameScheduler> scheduler() { return frame_scheduler_; }

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<Session> session_;
  std::shared_ptr<MockFrameScheduler> frame_scheduler_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
  std::shared_ptr<TestErrorReporter> error_reporter_;
  std::shared_ptr<TestEventReporter> event_reporter_;
};

VK_TEST_F(DisplaySwapchainTest, InitializesFramebuffers) {
  auto swapchain = CreateSwapchain(display());
  EXPECT_EQ(3u, Framebuffers(swapchain.get()).size());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
