// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/common/vk/vk_debug_report_callback_registry.h"

#include "src/ui/lib/escher/test/common/gtest_vulkan.h"

namespace escher::test {
namespace impl {

void VkDebugReportCallbackRegistry::RegisterDebugReportCallbacks() {
  if (!VK_TESTS_SUPPRESSED()) {
    FXL_CHECK(instance_ && optional_callback_handles_.size() == 0 && !main_callback_handle_);
    if (main_callback_) {
      main_callback_handle_ = instance_->RegisterDebugReportCallback(
          std::move(main_callback_->function), main_callback_->user_data);
    }
    for (auto& callback : optional_callbacks_) {
      optional_callback_handles_.push_back(
          instance_->RegisterDebugReportCallback(std::move(callback.function), callback.user_data));
    }
  }
}

void VkDebugReportCallbackRegistry::DeregisterDebugReportCallbacks() {
  if (!VK_TESTS_SUPPRESSED()) {
    FXL_CHECK(instance_ && optional_callback_handles_.size() == optional_callbacks_.size());
    if (main_callback_handle_) {
      instance_->DeregisterDebugReportCallback(*main_callback_handle_);
      main_callback_handle_ = std::nullopt;
    }
    for (const auto& callback_handle : optional_callback_handles_) {
      instance_->DeregisterDebugReportCallback(callback_handle);
    }
    optional_callback_handles_.clear();
  }
}

}  // namespace impl
}  // namespace escher::test
