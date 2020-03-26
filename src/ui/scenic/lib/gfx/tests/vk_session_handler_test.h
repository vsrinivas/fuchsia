// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_callback_registry.h"
#include "src/ui/lib/escher/test/common/vk/vk_debug_report_collector.h"
#include "src/ui/scenic/lib/gfx/tests/session_handler_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// A session handler test with full Vulkan and Escher support.
class VkSessionHandlerTest : public SessionHandlerTest {
 public:
  VkSessionHandlerTest();

  // |SessionHandlerTest|
  void SetUp() override;

  // |SessionHandlerTest|
  void TearDown() override;

  // |SessionHandlerTest|
  escher::EscherWeakPtr GetEscherWeakPtr() override;

  escher::Escher* escher() const { return escher_.get(); }

 protected:
  // Create Vulkan device for Escher setup. Only used in SetUp() method.
  static escher::VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory = false);

  escher::test::impl::VkDebugReportCallbackRegistry& vk_debug_report_callback_registry() {
    return vk_debug_report_callback_registry_;
  }

  escher::test::impl::VkDebugReportCollector& vk_debug_report_collector() {
    return vk_debug_report_collector_;
  }

 private:
  std::unique_ptr<escher::Escher> escher_;

  escher::test::impl::VkDebugReportCallbackRegistry vk_debug_report_callback_registry_;
  escher::test::impl::VkDebugReportCollector vk_debug_report_collector_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VK_SESSION_HANDLER_TEST_H_
