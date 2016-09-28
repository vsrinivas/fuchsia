// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "vkreadback.h"
}

#include "vulkan/vulkan.h"
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <stdio.h>
#include <string.h>

#include "magma_util/macros.h"

class VkReadbackTest {
public:
    bool Initialize();

private:
    bool InitVulkan();

    bool is_initialized_ = false;
};

bool VkReadbackTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan()) {
        printf("failed to initialize Vulkan\n");
        return false;
    }

    is_initialized_ = true;

    return true;
}

bool VkReadbackTest::InitVulkan()
{
    VkInstanceCreateInfo create_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, // VkStructureType             sType;
        nullptr,                                // const void*                 pNext;
        0,                                      // VkInstanceCreateFlags       flags;
        nullptr,                                // const VkApplicationInfo*    pApplicationInfo;
        0,                                      // uint32_t                    enabledLayerCount;
        nullptr,                                // const char* const*          ppEnabledLayerNames;
        0,       // uint32_t                    enabledExtensionCount;
        nullptr, // const char* const*          ppEnabledExtensionNames;
    };
    VkAllocationCallbacks* allocation_callbacks = nullptr;
    VkInstance instance;
    VkResult result;

    if ((result = vkCreateInstance(&create_info, allocation_callbacks, &instance)) != VK_SUCCESS)
        return DRETF(false, "vkCreateInstance failed %d", result);

    printf("vkCreateInstance succeeded\n");

    uint32_t physical_device_count;
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    if (physical_device_count < 1)
        return DRETF(false, "unexpected physical_device_count %d", physical_device_count);

    printf("vkEnumeratePhysicalDevices returned count %d\n", physical_device_count);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                             physical_devices.data())) != VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    for (auto device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        printf("PHYSICAL DEVICE: %s\n", properties.deviceName);
        printf("apiVersion 0x%x\n", properties.apiVersion);
        printf("driverVersion 0x%x\n", properties.driverVersion);
        printf("vendorID 0x%x\n", properties.vendorID);
        printf("deviceID 0x%x\n", properties.deviceID);
        printf("deviceType 0x%x\n", properties.deviceType);
        printf("etc...\n");
    }

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

    if (queue_family_count < 1)
        return DRETF(false, "invalid queue_family_count %d", queue_family_count);

    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count,
                                             queue_family_properties.data());

    int32_t queue_index = -1;
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_index = i;
            break;
        }
    }

    if (queue_index < 0)
        return DRETF(false, "couldn't find an appropriate queue");

    float queue_priorities[1] = {0.0};

    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .queueCount = 1,
                                                 .pQueuePriorities = queue_priorities};
    VkDeviceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue_create_info};
    VkDevice vkdevice;

    if ((result = vkCreateDevice(physical_devices[0], &createInfo,
                                 nullptr /* allocationcallbacks */, &vkdevice)) != VK_SUCCESS)
        return DRETF(false, "vkCreateDevice failed: %d", result);

    return true;
}

int test_vk_readback()
{
    VkReadbackTest app;
    if (!app.Initialize()) {
        printf("could not initialize app\n");
        return -1;
    }

    return 0;
}
