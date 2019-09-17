// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/test_with_vk_validation_layer_base.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/test/gtest_escher.h"

namespace escher::test {

void TestWithVkValidationLayerBase::RegisterAllDebugReportCallbacks() {
  const auto &instance = EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  for (auto &callback : optional_callbacks_) {
    callback_handles_.push_back(
        instance->RegisterDebugReportCallback(std::move(callback.function), callback.user_data));
  }
}

void TestWithVkValidationLayerBase::DeregisterAllDebugReportCallbacks() {
  FXL_CHECK(callback_handles_.size() == optional_callbacks_.size());
  const auto &instance = EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  for (const auto &callback_handle : callback_handles_) {
    instance->DeregisterDebugReportCallback(callback_handle);
  }
  callback_handles_.clear();
}

void TestWithVkValidationLayerBase::RegisterMainDebugReportCallback() {
  const auto &instance = EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  if (main_callback_) {
    main_callback_handle_ = instance->RegisterDebugReportCallback(
        std::move(main_callback_->function), main_callback_->user_data);
  }
}

void TestWithVkValidationLayerBase::DeregisterMainDebugReportCallback() {
  const auto &instance = EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  if (main_callback_handle_) {
    instance->DeregisterDebugReportCallback(*main_callback_handle_);
    main_callback_handle_ = std::nullopt;
  }
}

void TestWithVkValidationLayerBase::SetUp() {
  if (!VK_TESTS_SUPPRESSED()) {
    RegisterMainDebugReportCallback();
    RegisterAllDebugReportCallbacks();
  }
}

void TestWithVkValidationLayerBase::TearDown() {
  if (!VK_TESTS_SUPPRESSED()) {
    DeregisterMainDebugReportCallback();
    DeregisterAllDebugReportCallbacks();
  }
}


}  // namespace escher::test
