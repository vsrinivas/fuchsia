// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_VK_VK_DEBUG_REPORT_CALLBACK_REGISTRY_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_VK_VK_DEBUG_REPORT_CALLBACK_REGISTRY_H_

#include <utility>
#include <vector>

#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer_macros.h"
#include "src/ui/lib/escher/vk/vulkan_instance.h"

namespace escher::test {
namespace impl {

// Registry and storage of Vulkan validation callback functions
// used in |escher::test::TestWithVkValidationLayer|.
//
// A test fixture can have an instance of |VkDebugReportCallbackRegistry| as its member to register
// validation debug report callbacks; they need to set up callback functions in their initializer,
// and call |RegisterDebugReportCallbacks()| and |DeregisterDebugReportCallbacks()| functions
// explicitly in their own |SetUp()| and |TearDown()| functions.
//
class VkDebugReportCallbackRegistry {
 public:
  VkDebugReportCallbackRegistry(VulkanInstancePtr instance,
                                std::optional<VulkanInstance::DebugReportCallback> main_callback,
                                std::vector<VulkanInstance::DebugReportCallback> optional_callbacks)
      : instance_(instance),
        main_callback_(std::move(main_callback)),
        optional_callbacks_(std::move(optional_callbacks)) {}

  VulkanInstancePtr instance() const { return instance_; }

  void SetMainDebugReportCallback(VulkanInstance::DebugReportCallback callback) {
    FXL_CHECK(!main_callback_handle_);
    main_callback_ = std::make_optional(std::move(callback));
  }
  void SetOptionalDebugReportCallbacks(std::vector<VulkanInstance::DebugReportCallback> callbacks) {
    FXL_CHECK(optional_callback_handles_.empty());
    optional_callbacks_ = std::move(callbacks);
  }

  void RegisterDebugReportCallbacks();
  void DeregisterDebugReportCallbacks();

 private:
  VulkanInstancePtr instance_;
  std::optional<VulkanInstance::DebugReportCallback> main_callback_ = std::nullopt;
  std::optional<VulkanInstance::DebugReportCallbackHandle> main_callback_handle_ = std::nullopt;

  std::vector<VulkanInstance::DebugReportCallback> optional_callbacks_ = {};
  std::vector<VulkanInstance::DebugReportCallbackHandle> optional_callback_handles_ = {};
};

}  // namespace impl
}  // namespace escher::test
#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_VK_VK_DEBUG_REPORT_CALLBACK_REGISTRY_H_
