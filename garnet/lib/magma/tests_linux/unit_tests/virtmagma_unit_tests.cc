// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is intended to be run manually from within biscotti_guest.

#include "gtest/gtest.h"

#include "magma.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <vulkan/vulkan.h>

class VirtMagmaTest : public ::testing::Test {
protected:
    VirtMagmaTest() {}
    ~VirtMagmaTest() override {}
    int device_file_descriptor_;
    magma_connection_t connection_;
    void* driver_handle_;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;
    PFN_vkCreateInstance vkCreateInstance_;
    PFN_vkDestroyInstance vkDestroyInstance_;
    VkInstance instance_;
};

TEST_F(VirtMagmaTest, OpenDevice)
{
    static constexpr const char* kDevicePath = "/dev/wl0";
    device_file_descriptor_ = open(kDevicePath, O_NONBLOCK);
    ASSERT_GE(device_file_descriptor_, 0)
        << "Failed to open device " << kDevicePath << " (" << errno << ")";
}

TEST_F(VirtMagmaTest, MagmaQuery)
{
    uint64_t device_id = 0;
    magma_status_t status = magma_query(device_file_descriptor_, MAGMA_QUERY_DEVICE_ID, &device_id);
    EXPECT_EQ(status, MAGMA_STATUS_OK);
    EXPECT_NE(device_id, 0u);
}

TEST_F(VirtMagmaTest, MagmaCreateConnection)
{
    magma_status_t status = magma_create_connection(device_file_descriptor_, &connection_);
    EXPECT_EQ(status, MAGMA_STATUS_OK);
    EXPECT_NE(connection_, nullptr);
    magma_release_connection(connection_);
}

TEST_F(VirtMagmaTest, OpenDriver)
{
    static constexpr const char* kDriverPath = "/usr/lib64/libvulkan_magma.so";
    driver_handle_ = dlopen(kDriverPath, RTLD_NOW);
    ASSERT_NE(driver_handle_, nullptr)
        << "Failed to open driver " << kDriverPath << " (" << errno << ")";
}

TEST_F(VirtMagmaTest, GetVkGetInstanceProcAddress)
{
    static constexpr const char* kEntrypoint = "vk_icdGetInstanceProcAddr";
    vkGetInstanceProcAddr_ = (__typeof(vkGetInstanceProcAddr_))dlsym(driver_handle_, kEntrypoint);
    ASSERT_NE(vkGetInstanceProcAddr_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, GetVkCreateInstance)
{
    static constexpr const char* kEntrypoint = "vkCreateInstance";
    vkCreateInstance_ = (__typeof(vkCreateInstance_))vkGetInstanceProcAddr_(nullptr, kEntrypoint);
    ASSERT_NE(vkCreateInstance_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, CallVkCreateInstance)
{
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

TEST_F(VirtMagmaTest, GetVkDestroyInstance)
{
    static constexpr const char* kEntrypoint = "vkDestroyInstance";
    vkDestroyInstance_ = (__typeof(vkDestroyInstance_))vkGetInstanceProcAddr_(instance_, kEntrypoint);
    ASSERT_NE(vkDestroyInstance_, nullptr) << "Failed to get entrypoint " << kEntrypoint;
}

TEST_F(VirtMagmaTest, CallVkDestroyInstance)
{
    vkDestroyInstance_(instance_, nullptr);
}
