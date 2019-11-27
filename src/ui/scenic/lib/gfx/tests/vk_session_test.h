// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_

#include "src/ui/lib/escher/test/vk_debug_report_callback_registry.h"
#include "src/ui/lib/escher/test/vk_debug_report_collector.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class VkSessionTest : public SessionTest {
 public:
  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory = false);

  void SetUp() override;
  void TearDown() override;

  escher::Escher* escher() { return escher_.get(); }
  Sysmem* sysmem() { return sysmem_.get(); }
  display::DisplayManager* display_manager() { return display_manager_.get(); }

 protected:
  VkSessionTest();

  // |SessionTest|
  SessionContext CreateSessionContext() override;
  // |SessionTest|
  CommandContext CreateCommandContext() override;

  escher::test::impl::VkDebugReportCallbackRegistry& vk_debug_report_callback_registry() {
    return vk_debug_report_callback_registry_;
  }

  escher::test::impl::VkDebugReportCollector& vk_debug_report_collector() {
    return vk_debug_report_collector_;
  }

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<display::DisplayManager> display_manager_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;

  escher::test::impl::VkDebugReportCallbackRegistry vk_debug_report_callback_registry_;
  escher::test::impl::VkDebugReportCollector vk_debug_report_collector_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_TEST_H_
