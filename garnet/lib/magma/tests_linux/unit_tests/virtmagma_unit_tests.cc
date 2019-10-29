// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is intended to be run manually from within biscotti_guest.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>

#include <algorithm>
#include <fstream>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <vulkan/vulkan.h>

#include "gtest/gtest.h"
#include "magma.h"

class VirtMagmaTest : public ::testing::Test {
 protected:
  VirtMagmaTest() {}
  ~VirtMagmaTest() override {}
  magma_device_t device_ = 0;
  magma_connection_t connection_;
  void* driver_handle_;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;
  PFN_vkCreateInstance vkCreateInstance_;
  PFN_vkDestroyInstance vkDestroyInstance_;
  VkInstance instance_;
};

TEST_F(VirtMagmaTest, OpenDevice) {
  static constexpr const char* kDevicePath = "/dev/magma0";
  int device_file_descriptor = open(kDevicePath, O_NONBLOCK);

  ASSERT_GE(device_file_descriptor, 0)
      << "Failed to open device " << kDevicePath << " (" << errno << ")";
  magma_device_import(device_file_descriptor, &device_);
}

TEST_F(VirtMagmaTest, MagmaQuery) {
  uint64_t device_id = 0;
  magma_status_t status = magma_query2(device_, MAGMA_QUERY_DEVICE_ID, &device_id);
  EXPECT_EQ(status, MAGMA_STATUS_OK);
  EXPECT_NE(device_id, 0u);
}

TEST_F(VirtMagmaTest, MagmaCreateConnection) {
  magma_status_t status = magma_create_connection2(device_, &connection_);
  EXPECT_EQ(status, MAGMA_STATUS_OK);
  EXPECT_NE(connection_, nullptr);
  magma_release_connection(connection_);
}

TEST_F(VirtMagmaTest, OpenDriver) {
  static constexpr const char* kManifestOverrideVar = "VK_ICD_FILENAMES";
  char* manifest = getenv(kManifestOverrideVar);
  ASSERT_NE(manifest, nullptr) << "ICD Manifest File must be specified in the "
                               << kManifestOverrideVar << " environment variable";
  std::ifstream json(manifest);
  ASSERT_TRUE(json.is_open()) << "Failed to open manifest file \"" << manifest << "\"";
  rapidjson::IStreamWrapper isw(json);
  rapidjson::Document doc;
  doc.ParseStream(isw);
  ASSERT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.IsObject());
  auto& icd = doc["ICD"];
  ASSERT_TRUE(icd.IsObject());
  auto& path = icd["library_path"];
  ASSERT_TRUE(path.IsString());
  std::string driver(path.GetString());
  driver_handle_ = dlopen(driver.c_str(), RTLD_NOW);
  ASSERT_NE(driver_handle_, nullptr) << "Failed to open driver " << driver << " (" << errno << ")";
}

TEST_F(VirtMagmaTest, GetVkGetInstanceProcAddress) {
  static constexpr const char* kEntrypoint = "vk_icdGetInstanceProcAddr";
  vkGetInstanceProcAddr_ = (__typeof(vkGetInstanceProcAddr_))dlsym(driver_handle_, kEntrypoint);
  ASSERT_NE(vkGetInstanceProcAddr_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, GetVkCreateInstance) {
  static constexpr const char* kEntrypoint = "vkCreateInstance";
  vkCreateInstance_ = (__typeof(vkCreateInstance_))vkGetInstanceProcAddr_(nullptr, kEntrypoint);
  ASSERT_NE(vkCreateInstance_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, CallVkCreateInstance) {
  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = "VirtMagmaTest";
  application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.pEngineName = "no-engine";
  application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo instance_create_info{};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pApplicationInfo = &application_info;
  VkResult result = vkCreateInstance_(&instance_create_info, nullptr, &instance_);
  EXPECT_EQ(result, VK_SUCCESS);
  ASSERT_NE(instance_, nullptr);
}

TEST_F(VirtMagmaTest, GetVkDestroyInstance) {
  static constexpr const char* kEntrypoint = "vkDestroyInstance";
  vkDestroyInstance_ = (__typeof(vkDestroyInstance_))vkGetInstanceProcAddr_(instance_, kEntrypoint);
  ASSERT_NE(vkDestroyInstance_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, CallVkDestroyInstance) { vkDestroyInstance_(instance_, nullptr); }
