// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_

#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_callback_registry.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_collector.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scheduling/default_frame_scheduler.h"

namespace scenic_impl::gfx::test {

// A session handler test with full Vulkan and Escher support.
class VkSessionHandlerTest : public ErrorReportingTest {
 public:
  VkSessionHandlerTest();

  // |ErrorReportingTest|
  void SetUp() override;

  // |ErrorReportingTest|
  void TearDown() override;

  Session* session() const {
    FX_CHECK(command_dispatcher_);
    return static_cast<Session*>(command_dispatcher_.get());
  }
  std::shared_ptr<ImagePipeUpdater> image_pipe_updater() const { return image_pipe_updater_; }

 protected:
  class TestSessionUpdater : public scheduling::SessionUpdater {
   public:
    TestSessionUpdater(Engine* engine, SessionManager* session_manager,
                       ViewTreeUpdater* view_tree_updater)
        : engine_(engine),
          session_manager_(session_manager),
          view_tree_updater_(view_tree_updater) {}

    // |scheduling::SessionUpdater|
    UpdateResults UpdateSessions(
        const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
        uint64_t trace_id) override;
    // |scheduling::SessionUpdater|
    void OnFramePresented(
        const std::unordered_map<scheduling::SessionId,
                                 std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
            latched_times,
        scheduling::PresentTimestamps present_times) override {}
    // |scheduling::SessionUpdater|
    void OnCpuWorkDone() override {}

   private:
    Engine* engine_;
    SessionManager* session_manager_;
    ViewTreeUpdater* view_tree_updater_;
  };

  void InitializeScenic();
  void InitializeCommandDispatcher();
  void InitializeScenicSession(SessionId session_id);

  // Create Vulkan device for Escher setup. Only used in SetUp() method.
  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory = false);

  escher::test::impl::VkDebugReportCollector& vk_debug_report_collector() {
    return vk_debug_report_collector_;
  }

  sys::testing::ComponentContextProvider app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::shared_ptr<Engine> engine_;
  std::shared_ptr<scheduling::DefaultFrameScheduler> frame_scheduler_;
  std::unique_ptr<scenic_impl::Session> scenic_session_;
  std::unique_ptr<SessionManager> session_manager_;
  CommandDispatcherUniquePtr command_dispatcher_;
  std::shared_ptr<TestSessionUpdater> session_updater_;
  std::shared_ptr<ImagePipeUpdater> image_pipe_updater_;

  ViewTreeUpdater view_tree_updater_;

  std::unique_ptr<escher::Escher> escher_;

  escher::test::impl::VkDebugReportCallbackRegistry vk_debug_report_callback_registry_;
  escher::test::impl::VkDebugReportCollector vk_debug_report_collector_;
};

}  // namespace scenic_impl::gfx::test

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_
