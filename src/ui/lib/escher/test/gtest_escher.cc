// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/gtest_escher.h"

#include "src/ui/lib/escher/escher_process_init.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

Escher* GetEscher() {
  EXPECT_FALSE(VK_TESTS_SUPPRESSED());
  return EscherEnvironment::GetGlobalTestEnvironment()->GetEscher();
}

void EscherEnvironment::RegisterGlobalTestEnvironment() {
  FXL_CHECK(global_escher_environment_ == nullptr);
  global_escher_environment_ = static_cast<escher::test::EscherEnvironment*>(
      testing::AddGlobalTestEnvironment(new escher::test::EscherEnvironment));
}

EscherEnvironment* EscherEnvironment::GetGlobalTestEnvironment() {
  FXL_CHECK(global_escher_environment_ != nullptr);
  return global_escher_environment_;
}

void EscherEnvironment::SetUp() {
  if (!VK_TESTS_SUPPRESSED()) {
    VulkanInstance::Params instance_params(
        {{},
         {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
          VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME},
         false});
    auto validation_layer_name = VulkanInstance::GetValidationLayerName();
    if (validation_layer_name) {
      instance_params.layer_names.insert(*validation_layer_name);
    }

    VulkanDeviceQueues::Params device_params(
        {{VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_KHR_MAINTENANCE1_EXTENSION_NAME,
          VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
          VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME},
         {},
         vk::SurfaceKHR()});
#ifdef OS_FUCHSIA
    device_params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    device_params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
#endif
    vulkan_instance_ = VulkanInstance::New(instance_params);
    vulkan_device_ = VulkanDeviceQueues::New(vulkan_instance_, device_params);
    escher_ = std::make_unique<Escher>(vulkan_device_);

#if ESCHER_USE_RUNTIME_GLSL
    escher::GlslangInitializeProcess();
#endif
  }
}

void EscherEnvironment::TearDown() {
  if (!VK_TESTS_SUPPRESSED()) {
#if ESCHER_USE_RUNTIME_GLSL
    escher::GlslangFinalizeProcess();
#endif

    escher_.reset();
    vulkan_device_.reset();
    vulkan_instance_.reset();
  }
}

}  // namespace test
}  // namespace escher
