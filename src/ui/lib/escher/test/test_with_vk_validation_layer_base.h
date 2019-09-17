// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_BASE_H_
#define SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_BASE_H_

#include <gtest/gtest.h>
#include "src/ui/lib/escher/vk/vulkan_instance.h"

namespace escher::test {

// Googletest fixture class for Escher unit tests including support for Vulkan Validation layer.
//
// NOTE: This class DOES NOT work with all the validation macros (in 
// test_with_vk_validation_layer.h) and should be only used if the user needs a custom callback.
// Clients may want to use its subclass |TestWithVkValidationLayer| if they need validation macros.
//
// The test fixture have an |std::optional| main debug report callback, and we can add other
// extra debug report callback functions. The callback function can be functions, function pointers,
// or any lambda expressions of type:
//   VkBool32 Function (VkDebugReportFlagsEXT flags_in, VkDebugReportObjectTypeEXT object_type_in,
//                      uint64_t object, size_t location, int32_t message_code,
//                      const char *pLayerPrefix, const char *pMessage, void *pUserData);
// The pointer value of pUserData is specified by user.
//
// When Vulkan validation layer has a message to report, it will call each callback function.
//
// The test suite ValidationLayerCustomHandler in file "test/vk/validation_layer_test.cc" shows
// an example about how to extend this test suite class for custom debug report handlers.
//
class TestWithVkValidationLayerBase : public ::testing::Test {
 public:
 protected:
  TestWithVkValidationLayerBase() : TestWithVkValidationLayerBase(std::nullopt,
                                                                  {}) {}

  explicit TestWithVkValidationLayerBase(VulkanInstance::DebugReportCallback main_callback)
      : TestWithVkValidationLayerBase(std::make_optional(std::move(main_callback)),
                                      {}) {}

  explicit TestWithVkValidationLayerBase(
      std::optional<VulkanInstance::DebugReportCallback> main_callback,
      std::vector<VulkanInstance::DebugReportCallback> optional_callbacks) {
    main_callback_ = std::move(main_callback);
    optional_callbacks_ = std::move(optional_callbacks);
  }

  void SetMainDebugReportCallback(VulkanInstance::DebugReportCallback callback) {
    FXL_CHECK(!main_callback_handle_);
    main_callback_ = std::make_optional(std::move(callback));
  }

  void SetOptionalDebugReportCallbacks(std::vector<VulkanInstance::DebugReportCallback> callbacks) {
    FXL_CHECK(callback_handles_.empty());
    optional_callbacks_ = std::move(callbacks);
  }

  // Overrides |::testing::Test|. |SetUp()| registers all debug report callback functions
  // (including main callback and optional callbacks).
  //
  // Note: For all derived class, if they need to override this function, call this function first
  // in the new |SetUp()| function:
  //
  // void SetUp() override {
  //   TestWithVkValidationLayerBase::SetUp();
  //   ... // do something
  // }
  void SetUp() override;

  // Overrides |::testing::Test|. |TearDown()| deregisters all debug report callback functions
  // (including main callback and optional callbacks).
  //
  // Note: For all derived class, if they need to override this function, call this function in the
  // end of the new |TearDown()| function:
  //
  // void TearDown() override {
  //  ... // do something
  //   TestWithVkValidationLayerBase::TearDown();
  // }
  void TearDown() override;

 private:
  void RegisterMainDebugReportCallback();
  void DeregisterMainDebugReportCallback();

  void RegisterAllDebugReportCallbacks();
  void DeregisterAllDebugReportCallbacks();

  std::optional<VulkanInstance::DebugReportCallback>
      main_callback_ = std::nullopt;
  std::optional<VulkanInstance::DebugReportCallbackHandle>
      main_callback_handle_ = std::nullopt;

  std::vector<VulkanInstance::DebugReportCallback> optional_callbacks_ = {};
  std::vector<VulkanInstance::DebugReportCallbackHandle> callback_handles_ = {};
};

} // namespace escher::test

#endif  // SRC_UI_LIB_ESCHER_TEST_TEST_WITH_VK_VALIDATION_LAYER_BASE_H_
