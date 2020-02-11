// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_GTEST_ESCHER_H_
#define SRC_UI_LIB_ESCHER_TEST_GTEST_ESCHER_H_

#include <gtest/gtest.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/test/test_with_vk_validation_layer_macros_internal.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/test/test_with_vk_validation_layer.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

// Must call during tests, only if !VK_TESTS_SUPPRESSED().
// |escher::test::EscherEnvironment::RegisterGlobalTestEnvironment()| should be already called.
// (Generatlly this should be called before RUN_ALL_TESTS() is invoked in |run_all_unittests.cc|.)
Escher* GetEscher();

// Googletest Environment containing Vulkan instance, Vulkan device and Escher instance.
// This class will be created before RUN_ALL_TESTS() and be recycled automatically after all test
// cases finish running.
class EscherEnvironment : public ::testing::Environment {
 public:
  // Register EscherEnvironment as a Googletest global test environment. The
  // environment will be managed by Googletest after being registered.
  static void RegisterGlobalTestEnvironment();
  static EscherEnvironment* GetGlobalTestEnvironment();

  void SetUp() override;
  void TearDown() override;

  Escher* GetEscher() {
    FXL_DCHECK(!VK_TESTS_SUPPRESSED());
    return escher_.get();
  }
  VulkanDeviceQueuesPtr GetVulkanDevice() {
    FXL_DCHECK(!VK_TESTS_SUPPRESSED());
    return vulkan_device_;
  }
  VulkanInstancePtr GetVulkanInstance() {
    FXL_DCHECK(!VK_TESTS_SUPPRESSED());
    return vulkan_instance_;
  }

 private:
  VulkanInstancePtr vulkan_instance_;
  VulkanDeviceQueuesPtr vulkan_device_;
  std::unique_ptr<Escher> escher_;

  // The global Escher testing environment. Managed by Googletest after initial
  // creation and will be recycled after running all unit tests.
  inline static EscherEnvironment* global_escher_environment_ = nullptr;
};

}  // namespace test
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_GTEST_ESCHER_H_
