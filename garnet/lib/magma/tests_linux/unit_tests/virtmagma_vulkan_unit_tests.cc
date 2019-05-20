// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <sstream>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

// Convert an integer to upper-case zero-extended hex string
template <class T> std::string tohex(T x)
{
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(sizeof(x) * 2) << x;
    return o.str();
}

struct VulkanPhysicalDevice {
    VkPhysicalDevice device;
    VkPhysicalDeviceProperties properties;
    std::vector<VkQueueFamilyProperties> queues;
};

class VirtMagmaTest : public ::testing::Test {
protected:
    VirtMagmaTest() {}

    ~VirtMagmaTest() override {}

    void SetUp() override
    {
        CreateInstance();
        EnumeratePhysicalDevices();
        GetQueues();
    }

    void TearDown() override
    {
        physical_devices_.clear(); // Implicitly destroyed with instance.
        vkDestroyInstance(instance_, nullptr);
    }

    void CreateInstance()
    {
        VkApplicationInfo application_info{};
        application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application_info.pApplicationName = "fuchsia-test";
        application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application_info.pEngineName = "no-engine";
        application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application_info.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instance_create_info{};
        instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo = &application_info;
        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &instance_);
        ASSERT_EQ(result, VK_SUCCESS) << "vkCreateInstance failed";
    }

    void EnumeratePhysicalDevices()
    {
        uint32_t physical_device_count = 0;
        std::vector<VkPhysicalDevice> physical_devices;
        VkResult result = vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
        ASSERT_EQ(result, VK_SUCCESS) << "vkEnumeratePhysicalDevices failed";
        physical_devices.resize(physical_device_count);
        while ((result = vkEnumeratePhysicalDevices(instance_, &physical_device_count,
                                                    physical_devices.data())) == VK_INCOMPLETE) {
            physical_devices.resize(++physical_device_count);
        }
        ASSERT_EQ(result, VK_SUCCESS) << "vkEnumeratePhysicalDevices failed";
        ASSERT_GT(physical_devices.size(), 0u) << "No physical devices found";
        physical_devices_.resize(physical_device_count);
        for (uint32_t i = 0; i < physical_device_count; ++i) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical_devices[i], &properties);
            EXPECT_NE(properties.vendorID, 0u) << "Missing vendor ID";
            EXPECT_NE(properties.deviceID, 0u) << "Missing device ID";
            EXPECT_LE(properties.vendorID, 0xFFFFu) << "Invalid vendor ID";
            EXPECT_LE(properties.deviceID, 0xFFFFu) << "Invalid device ID";
            physical_devices_[i].device = physical_devices[i];
            physical_devices_[i].properties = properties;
        }
    }

    void GetQueues()
    {
        for (auto& device : physical_devices_) {
            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device.device, &queue_count, nullptr);
            ASSERT_GT(queue_count, 0u) << "No queue families found";
            device.queues.resize(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device.device, &queue_count,
                                                     device.queues.data());
            uint32_t queue_flags_union = 0;
            for (auto& queue : device.queues) {
                ASSERT_GT(queue.queueCount, 0u) << "Empty queue family";
                queue_flags_union |= queue.queueFlags;
            }
            ASSERT_TRUE(queue_flags_union & VK_QUEUE_GRAPHICS_BIT)
                << "Device missing graphics capability";
            ASSERT_TRUE(queue_flags_union & VK_QUEUE_COMPUTE_BIT)
                << "Device missing compute capability";
        }
    }

    VkInstance instance_;
    std::vector<VulkanPhysicalDevice> physical_devices_;
};

// Tests that a device can be created on the first reported graphics queue.
TEST_F(VirtMagmaTest, CreateGraphicsDevice)
{
    // TODO(MA-619): support per-device gtests
    for (auto& physical_device : physical_devices_) {
        VkDeviceQueueCreateInfo device_queue_create_info{};
        device_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        for (uint32_t i = 0; i < physical_device.queues.size(); ++i) {
            if (physical_device.queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                device_queue_create_info.queueFamilyIndex = i;
                break;
            }
        }
        device_queue_create_info.queueCount = 1;
        float priority = 1.0f;
        device_queue_create_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_create_info{};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount = 1;
        device_create_info.pQueueCreateInfos = &device_queue_create_info;
        VkDevice device{};
        VkResult result =
            vkCreateDevice(physical_device.device, &device_create_info, nullptr, &device);
        ASSERT_EQ(result, VK_SUCCESS) << "vkCreateDevice failed";
        vkDestroyDevice(device, nullptr);
    }
}

} // namespace
