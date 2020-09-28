// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_ESCHER_H_
#define SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_ESCHER_H_

#include <gtest/gtest.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/escher_process_init.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer.h"
#include "src/ui/lib/escher/test/common/test_with_vk_validation_layer_macros_internal.h"

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
    FX_DCHECK(!VK_TESTS_SUPPRESSED());
    return escher_.get();
  }
  VulkanDeviceQueuesPtr GetVulkanDevice() {
    FX_DCHECK(!VK_TESTS_SUPPRESSED());
    return vulkan_device_;
  }
  VulkanInstancePtr GetVulkanInstance() {
    FX_DCHECK(!VK_TESTS_SUPPRESSED());
    return vulkan_instance_;
  }
  HackFilesystemPtr GetFilesystem() {
    FX_DCHECK(!VK_TESTS_SUPPRESSED());
    return hack_filesystem_;
  }

 private:
  VulkanInstancePtr vulkan_instance_;
  VulkanDeviceQueuesPtr vulkan_device_;
  HackFilesystemPtr hack_filesystem_;
  std::unique_ptr<Escher> escher_;

  // The global Escher testing environment. Managed by Googletest after initial
  // creation and will be recycled after running all unit tests.
  inline static EscherEnvironment* global_escher_environment_ = nullptr;
};

// Checks if the global Escher environment uses SwiftShader as its physical
// device. This is used in macro SKIP_TEST_IF_ESCHER_USES_DEVICE().
bool GlobalEscherUsesSwiftShader();

// Checks if the global Escher environment uses an Virtual GPU as its physical
// device (for example, on FEMU). This is used in macro
// SKIP_TEST_IF_ESCHER_USES_DEVICE().
bool GlobalEscherUsesVirtualGpu();

// Skip the test if Escher uses a specific device or a specific type of device.
// TODO(fxbug.dev/49863), TODO(fxbug.dev/54086): This is a workaround since some tests doesn't work
// on SwiftShader ICD and FEMU. We should remove this macro after these issues
// are resolved.
#define SKIP_TEST_IF_ESCHER_USES_DEVICE(DeviceType)                                          \
  do {                                                                                       \
    if (escher::test::GlobalEscherUses##DeviceType()) {                                      \
      FX_LOGS(WARNING) << "This test doesn't work on " #DeviceType " device; Test skipped."; \
      GTEST_SKIP();                                                                          \
    }                                                                                        \
  } while (0)

// Execute the statements only if Escher doesn't use SwiftShader ICD.
// TODO(fxbug.dev/49863): This is a workaround since some tests doesn't work on
// SwiftShader ICD. We should remove this macro after these issues are
// resolved.
#define EXEC_IF_NOT_SWIFTSHADER(stmt)                   \
  do {                                                  \
    if (!escher::test::GlobalEscherUsesSwiftShader()) { \
      stmt;                                             \
    }                                                   \
  } while (0)

}  // namespace test
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_COMMON_GTEST_ESCHER_H_
