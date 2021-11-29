// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/vk_session_handler_test.h"

#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"
#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"

using namespace escher;

namespace scenic_impl::gfx::test {

VkSessionHandlerTest::VkSessionHandlerTest()
    : vk_debug_report_callback_registry_(
          escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance(),
          std::make_optional<VulkanInstance::DebugReportCallback>(
              escher::test::impl::VkDebugReportCollector::HandleDebugReport,
              &vk_debug_report_collector_),
          {}),
      vk_debug_report_collector_() {}

void VkSessionHandlerTest::SetUp() {
  escher_ = std::make_unique<Escher>(CreateVulkanDeviceQueues(false));

  ErrorReportingTest::SetUp();

  InitializeScenic();
  InitializeCommandDispatcher();

  RunLoopUntilIdle();  // Reset loop state; some tests are sensitive to dirty loop state.
}

void VkSessionHandlerTest::TearDown() {
  ErrorReportingTest::TearDown();

  escher_->vk_device().waitIdle();
  EXPECT_TRUE(escher_->Cleanup());
  EXPECT_VULKAN_VALIDATION_OK();
}

void VkSessionHandlerTest::InitializeScenic() {
  scenic_ = std::make_unique<Scenic>(
      app_context_.context(), inspect::Node(), [] {}, /*use_flatland=*/false);
  frame_scheduler_ = std::make_shared<scheduling::DefaultFrameScheduler>(
      std::make_shared<scheduling::VsyncTiming>(),
      std::make_unique<scheduling::ConstantFramePredictor>(/* static_vsync_offset */ zx::msec(5)));
  engine_ = std::make_shared<Engine>(escher_->GetWeakPtr());

  session_manager_ = std::make_unique<SessionManager>();
  session_updater_ = std::make_shared<TestSessionUpdater>(engine_.get(), session_manager_.get(),
                                                          &view_tree_updater_);
  image_pipe_updater_ = std::make_shared<ImagePipeUpdater>(frame_scheduler_);
  frame_scheduler_->Initialize(engine_, {session_updater_, image_pipe_updater_});
}

void VkSessionHandlerTest::InitializeCommandDispatcher() {
  auto session_context = engine_->session_context();
  auto session_id = SessionId(1);

  InitializeScenicSession(session_id);

  command_dispatcher_ = session_manager_->CreateCommandDispatcher(
      scenic_session_->id(), std::move(session_context), this->shared_event_reporter(),
      this->shared_error_reporter());
}

void VkSessionHandlerTest::InitializeScenicSession(SessionId session_id) {
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener;
  scenic_session_ = std::make_unique<scenic_impl::Session>(
      session_id, /*session_request=*/nullptr, std::move(listener), [this, session_id]() {
        scenic_->CloseSession(session_id);
        scenic_session_.reset();
      });
}

scheduling::SessionUpdater::UpdateResults VkSessionHandlerTest::TestSessionUpdater::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  UpdateResults update_results;
  CommandContext command_context = {.scene_graph = engine_->scene_graph()->GetWeakPtr(),
                                    .view_tree_updater = view_tree_updater_};

  for (auto [session_id, present_id] : sessions_to_update) {
    auto session = session_manager_->FindSession(session_id);
    if (session) {
      session->ApplyScheduledUpdates(&command_context, present_id);
    }
  }

  return update_results;
}

VulkanDeviceQueuesPtr VkSessionHandlerTest::CreateVulkanDeviceQueues(bool use_protected_memory) {
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  // This extension is necessary to support exporting Vulkan memory to a VMO.
  VulkanDeviceQueues::Params::Flags flags =
      use_protected_memory ? VulkanDeviceQueues::Params::kAllowProtectedMemory : 0;
  auto vulkan_queues = VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME},
       {},
       vk::SurfaceKHR(),
       flags});
  // Some devices might not be capable of using protected memory.
  if (use_protected_memory && !vulkan_queues->caps().allow_protected_memory) {
    return nullptr;
  }
  return vulkan_queues;
}

}  // namespace scenic_impl::gfx::test
