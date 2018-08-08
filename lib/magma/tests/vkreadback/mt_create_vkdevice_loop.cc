// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

#include "magma_util/macros.h"

class VkReadbackTest {
public:
    static constexpr uint32_t kWidth = 64;
    static constexpr uint32_t kHeight = 64;

    bool Initialize();
    bool Exec();
    bool Readback();

private:
    bool InitVulkan();
    bool InitImage();

    bool is_initialized_ = false;
    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue vk_queue_;
    VkImage vk_image_;
    VkDeviceMemory vk_device_memory_;
    VkCommandPool vk_command_pool_;
    VkCommandBuffer vk_command_buffer_;
};

bool VkReadbackTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    if (!InitImage())
        return DRETF(false, "InitImage failed");

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

    int32_t queue_family_index = -1;
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = i;
            break;
        }
    }

    if (queue_family_index < 0)
        return DRETF(false, "couldn't find an appropriate queue");

    int count = 0;

    VkPhysicalDevice p_d = physical_devices[0];

    while (true) {

        std::thread first([p_d] {
            float queue_priorities[1] = {0.0};

            VkDeviceQueueCreateInfo queue_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
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

            VkResult result;
            if ((result = vkCreateDevice(p_d, &createInfo, nullptr /* allocationcallbacks */,
                                         &vkdevice)) != VK_SUCCESS)
                printf("vkCreateDevice failed: %d\n", result);
            vkDestroyDevice(vkdevice, nullptr);
        });

        std::thread second([p_d] {
            float queue_priorities[1] = {0.0};

            VkDeviceQueueCreateInfo queue_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
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

            VkResult result;
            if ((result = vkCreateDevice(p_d, &createInfo, nullptr /* allocationcallbacks */,
                                         &vkdevice)) != VK_SUCCESS)
                printf("vkCreateDevice failed: %d\n", result);
            vkDestroyDevice(vkdevice, nullptr);
        });

        first.join();
        second.join();

        printf("device create count: %d\n", ++count);
    }

    vk_physical_device_ = physical_devices[0];
    // vk_device_ = vkdevice;

    vkGetDeviceQueue(vk_device_, queue_family_index, 0, &vk_queue_);

    return true;
}

bool VkReadbackTest::InitImage()
{
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = VkExtent3D{kWidth, kHeight, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,     // not used since not sharing
        .pQueueFamilyIndices = nullptr, // not used since not sharing
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result;

    if ((result = vkCreateImage(vk_device_, &image_create_info, nullptr, &vk_image_)) != VK_SUCCESS)
        return DRETF(false, "vkCreateImage failed: %d", result);

    printf("Created image\n");

    VkMemoryRequirements memory_reqs;
    vkGetImageMemoryRequirements(vk_device_, vk_image_, &memory_reqs);

    VkPhysicalDeviceMemoryProperties memory_props;
    vkGetPhysicalDeviceMemoryProperties(vk_physical_device_, &memory_props);

    uint32_t memory_type = 0;
    for (; memory_type < 32; memory_type++) {
        if ((memory_reqs.memoryTypeBits & (1 << memory_type)) &&
            (memory_props.memoryTypes[memory_type].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            break;
    }
    if (memory_type >= 32)
        return DRETF(false, "Can't find compatible mappable memory for image");

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memory_reqs.size,
        .memoryTypeIndex = memory_type,
    };

    if ((result = vkAllocateMemory(vk_device_, &alloc_info, nullptr, &vk_device_memory_)) !=
        VK_SUCCESS)
        return DRETF(false, "vkAllocateMemory failed");

    void* addr;
    if ((result = vkMapMemory(vk_device_, vk_device_memory_, 0, VK_WHOLE_SIZE, 0, &addr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkMapMeory failed: %d", result);

    memset(addr, 0xab, memory_reqs.size);

    vkUnmapMemory(vk_device_, vk_device_memory_);

    printf("Allocated memory for image\n");

    if ((result = vkBindImageMemory(vk_device_, vk_image_, vk_device_memory_, 0)) != VK_SUCCESS)
        return DRETF(false, "vkBindImageMemory failed");

    printf("Bound memory to image\n");

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = 0,
    };
    if ((result = vkCreateCommandPool(vk_device_, &command_pool_create_info, nullptr,
                                      &vk_command_pool_)) != VK_SUCCESS)
        return DRETF(false, "vkCreateCommandPool failed: %d", result);

    printf("Created command buffer pool\n");

    VkCommandBufferAllocateInfo command_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vk_command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    if ((result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info,
                                           &vk_command_buffer_)) != VK_SUCCESS)
        return DRETF(false, "vkAllocateCommandBuffers failed: %d", result);

    printf("Created command buffer\n");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr, // ignored for primary buffers
    };
    if ((result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info)) != VK_SUCCESS)
        return DRETF(false, "vkBeginCommandBuffer failed: %d", result);

    printf("Command buffer begin\n");

    VkClearColorValue color_value = {.float32 = {1.0f, 0.0f, 0.5f, 0.75f}};

    VkImageSubresourceRange image_subres_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    vkCmdClearColorImage(vk_command_buffer_, vk_image_, VK_IMAGE_LAYOUT_GENERAL, &color_value, 1,
                         &image_subres_range);

    if ((result = vkEndCommandBuffer(vk_command_buffer_)) != VK_SUCCESS)
        return DRETF(false, "vkEndCommandBuffer failed: %d", result);

    printf("Command buffer end\n");

    return true;
}

bool VkReadbackTest::Exec()
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

bool VkReadbackTest::Readback()
{
    VkResult result;
    void* addr;

    if ((result = vkMapMemory(vk_device_, vk_device_memory_, 0, VK_WHOLE_SIZE, 0, &addr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkMapMeory failed: %d", result);

    auto data = reinterpret_cast<uint32_t*>(addr);

    uint32_t expected_value = 0xBF8000FF;
    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < kWidth * kHeight; i++) {
        if (data[i] != expected_value) {
            if (mismatches++ < 10)
                printf("Value Mismatch at index %d - expected 0x%04x, got 0x%08x\n", i,
                       expected_value, data[i]);
        }
    }
    if (mismatches) {
        printf("****** Test Failed! %d mismatches\n", mismatches);
    } else {
        printf("****** Test Passed! All values matched.\n");
    }

    vkUnmapMemory(vk_device_, vk_device_memory_);

    return mismatches == 0;
}

int main(void)
{
#if defined(MAGMA_USE_SHIM)
    VulkanShimInit();
#endif

    VkReadbackTest app;

    if (!app.Initialize())
        return DRET_MSG(-1, "could not initialize app");

    if (!app.Exec())
        return DRET_MSG(-1, "Exec failed");

    if (!app.Readback())
        return DRET_MSG(-1, "Readback failed");

    return 0;
}
