// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_test.h"

using VirtioMagmaGuestTest = GuestTest<TerminaEnclosedGuest>;

static constexpr const char* kDevicePath = "/dev/magma0";
static const std::unordered_map<std::string, std::string> kMagmaEnv = {
    {"VK_ICD_FILENAMES", "/tmp/extras/magma.json"},
};

TEST_F(VirtioMagmaGuestTest, DeviceProperties) {
  int32_t return_code = 0;
  ASSERT_EQ(this->Execute({"test", "-e", kDevicePath}, nullptr, &return_code), ZX_OK);
  ASSERT_EQ(return_code, 0) << kDevicePath << " does not exist";
  ASSERT_EQ(this->Execute({"test", "-c", kDevicePath}, nullptr, &return_code), ZX_OK);
  ASSERT_EQ(return_code, 0) << kDevicePath << " is not a character device";
}

TEST_F(VirtioMagmaGuestTest, MagmaConformance) {
  std::string text;
  int32_t return_code = 0;
  ASSERT_EQ(this->Execute({"/tmp/extras/virtmagma_abi_conformance_tests"}, kMagmaEnv, &text,
                          &return_code),
            ZX_OK);
  ASSERT_EQ(return_code, 0) << "[BEGIN GUEST TEXT]" << text << "[END GUEST TEXT]";
}

TEST_F(VirtioMagmaGuestTest, MagmaUnit) {
  std::string text;
  int32_t return_code = 0;
  ASSERT_EQ(
      this->Execute({"/tmp/extras/virtmagma_unit_tests_linux"}, kMagmaEnv, &text, &return_code),
      ZX_OK);
  ASSERT_EQ(return_code, 0) << "[BEGIN GUEST TEXT]" << text << "[END GUEST TEXT]";
}

TEST_F(VirtioMagmaGuestTest, VulkanUnit) {
  std::string text;
  int32_t return_code = 0;
  ASSERT_EQ(
      this->Execute({"/tmp/extras/virtmagma_vulkan_unit_tests"}, kMagmaEnv, &text, &return_code),
      ZX_OK);
  ASSERT_EQ(return_code, 0) << "[BEGIN GUEST TEXT]" << text << "[END GUEST TEXT]";
}
