// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <vector>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

#include "garnet/lib/magma/tests/helper/config_namespace_helper.h"

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif

class VkCopyTest {
public:
    VkCopyTest(uint32_t buffer_size) : buffer_size_(buffer_size) {}

    bool Initialize();
    bool Exec();

private:
    bool InitVulkan();
    bool InitBuffers(uint32_t buffer_size);

    bool is_initialized_ = false;
    uint32_t buffer_size_;
    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue vk_queue_;
    VkBuffer vk_buffer_[2];
    VkDeviceMemory vk_device_memory_[0];
    VkCommandPool vk_command_pool_;
    VkCommandBuffer vk_command_buffer_;
};

bool VkCopyTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    if (!InitBuffers(buffer_size_))
        return DRETF(false, "InitImage failed");

    is_initialized_ = true;

    return true;
}

bool VkCopyTest::InitVulkan()
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

    result = vkCreateInstance(&create_info, allocation_callbacks, &instance);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkCreateInstance failed %d", result);

    DLOG("vkCreateInstance succeeded");

    uint32_t physical_device_count;
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    if (physical_device_count < 1)
        return DRETF(false, "unexpected physical_device_count %d", physical_device_count);

    DLOG("vkEnumeratePhysicalDevices returned count %d", physical_device_count);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data());
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    for (auto device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        DLOG("PHYSICAL DEVICE: %s", properties.deviceName);
        DLOG("apiVersion 0x%x", properties.apiVersion);
        DLOG("driverVersion 0x%x", properties.driverVersion);
        DLOG("vendorID 0x%x", properties.vendorID);
        DLOG("deviceID 0x%x", properties.deviceID);
        DLOG("deviceType 0x%x", properties.deviceType);
    }

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

    if (queue_family_count < 1)
        return DRETF(false, "invalid queue_family_count %d", queue_family_count);

    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count,
                                             queue_family_properties.data());

    int32_t queue_family_index = -1;
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = i;
            break;
        }
    }

    if (queue_family_index < 0)
        return DRETF(false, "couldn't find an appropriate queue");

    float queue_priorities[1] = {0.0};

    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .queueFamilyIndex = 0,
                                                 .queueCount = 1,
                                                 .pQueuePriorities = queue_priorities};
    VkDeviceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue_create_info,
                                     .enabledLayerCount = 0,
                                     .ppEnabledLayerNames = nullptr,
                                     .enabledExtensionCount = 0,
                                     .ppEnabledExtensionNames = nullptr,
                                     .pEnabledFeatures = nullptr};
    VkDevice vkdevice;

    if ((result = vkCreateDevice(physical_devices[0], &createInfo,
                                 nullptr /* allocationcallbacks */, &vkdevice)) != VK_SUCCESS)
        return DRETF(false, "vkCreateDevice failed: %d", result);

    vk_physical_device_ = physical_devices[0];
    vk_device_ = vkdevice;

    vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

    return true;
}

bool VkCopyTest::InitBuffers(uint32_t buffer_size)
{
    VkResult result;

    VkPhysicalDeviceMemoryProperties memory_props;
    vkGetPhysicalDeviceMemoryProperties(vk_physical_device_, &memory_props);

    uint32_t memory_type = 0;
    for (; memory_type < 32; memory_type++) {
        if (memory_props.memoryTypes[memory_type].propertyFlags &
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            break;
    }

    if (memory_type >= 32)
        return DRETF(false, "Can't find compatible mappable memory for image");

    for (uint32_t i = 0; i < 2; i++) {
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = buffer_size,
            .usage = (i == 0) ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };

        result = vkCreateBuffer(vk_device_, &create_info, nullptr, &vk_buffer_[i]);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkCreateBuffer failed: %d", result);

        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = buffer_size,
            .memoryTypeIndex = memory_type,
        };

        result = vkAllocateMemory(vk_device_, &alloc_info, nullptr, &vk_device_memory_[i]);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkAllocateMemory failed: %d", result);

        void* addr;
        result = vkMapMemory(vk_device_, vk_device_memory_[i], 0, VK_WHOLE_SIZE, 0, &addr);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkMapMeory failed: %d", result);

        memset(addr, (uint8_t)i, buffer_size);

        vkUnmapMemory(vk_device_, vk_device_memory_[i]);

        DLOG("Allocated and initialized buffer %d", i);

        result = vkBindBufferMemory(vk_device_, vk_buffer_[i], vk_device_memory_[i], 0);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkBindBufferMemory failed: %d", result);

        DLOG("Bound memory to buffer");
    }

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = 0,
    };

    result = vkCreateCommandPool(vk_device_, &command_pool_create_info, nullptr, &vk_command_pool_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkCreateCommandPool failed: %d", result);

    DLOG("Created command buffer pool");

    VkCommandBufferAllocateInfo command_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vk_command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info, &vk_command_buffer_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkAllocateCommandBuffers failed: %d", result);

    DLOG("Created command buffer");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr, // ignored for primary buffers
    };
    result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkBeginCommandBuffer failed: %d", result);

    DLOG("Command buffer begin");

    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = buffer_size,
    };

    vkCmdCopyBuffer(vk_command_buffer_, vk_buffer_[0], vk_buffer_[1], 1, &copy_region);

    result = vkEndCommandBuffer(vk_command_buffer_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEndCommandBuffer failed: %d", result);

    DLOG("Command buffer end");

    return true;
}

bool VkCopyTest::Exec()
{
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &vk_command_buffer_,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    VkResult result;
    if ((result = vkQueueSubmit(vk_queue_, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed");

    vkQueueWaitIdle(vk_queue_);

    return true;
}

int main(void)
{
#if defined(MAGMA_USE_SHIM)
    VulkanShimInit();
#endif

    if (!InstallConfigDirectoryIntoGlobalNamespace()) {
        return 1;
    }

    uint32_t buffer_size = 60 * 1024 * 1024;
    uint32_t iterations = 1000;

    VkCopyTest app(buffer_size);

    if (!app.Initialize())
        return DRET_MSG(-1, "could not initialize app");

    printf("Copying buffer_size %u iterations %u...\n", buffer_size, iterations);
    fflush(stdout);

    auto start = std::chrono::high_resolution_clock::now();

    for (uint32_t iter = 0; iter < iterations; iter++) {
        if (!app.Exec())
            return DRET_MSG(-1, "Exec failed");
    }

    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;

    printf("copy rate %g MB/s\n", (double)buffer_size * iterations / 1024 / 1024 / elapsed.count());

    return 0;
}
