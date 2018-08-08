// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace {

class VkLoopTest {
public:
    explicit VkLoopTest(bool hang_on_event) : hang_on_event_(hang_on_event) {}

    bool Initialize();
    bool Exec();

private:
    bool InitVulkan();
    bool InitCommandBuffer();

    bool hang_on_event_;
    bool is_initialized_ = false;
    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue vk_queue_;

    VkCommandPool vk_command_pool_;
    VkCommandBuffer vk_command_buffer_;
};

bool VkLoopTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    if (!InitCommandBuffer())
        return DRETF(false, "InitImage failed");

    is_initialized_ = true;

    return true;
}

bool VkLoopTest::InitVulkan()
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

    DLOG("vkCreateInstance succeeded");

    uint32_t physical_device_count;
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    if (physical_device_count < 1)
        return DRETF(false, "unexpected physical_device_count %d", physical_device_count);

    DLOG("vkEnumeratePhysicalDevices returned count %d", physical_device_count);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                             physical_devices.data())) != VK_SUCCESS)
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
        if (queue_family_properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
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

    std::vector<const char*> enabled_extension_names;

    VkDeviceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue_create_info,
                                     .enabledLayerCount = 0,
                                     .ppEnabledLayerNames = nullptr,
                                     .enabledExtensionCount =
                                         static_cast<uint32_t>(enabled_extension_names.size()),
                                     .ppEnabledExtensionNames = enabled_extension_names.data(),
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

bool VkLoopTest::InitCommandBuffer()
{
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = 0,
    };
    VkResult result;
    if ((result = vkCreateCommandPool(vk_device_, &command_pool_create_info, nullptr,
                                      &vk_command_pool_)) != VK_SUCCESS)
        return DRETF(false, "vkCreateCommandPool failed: %d", result);
    DLOG("Created command buffer pool");

    VkCommandBufferAllocateInfo command_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vk_command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    if ((result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info,
                                           &vk_command_buffer_)) != VK_SUCCESS)
        return DRETF(false, "vkAllocateCommandBuffers failed: %d", result);

    DLOG("Created command buffer");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr, // ignored for primary buffers
    };
    if ((result = vkBeginCommandBuffer(vk_command_buffer_, &begin_info)) != VK_SUCCESS)
        return DRETF(false, "vkBeginCommandBuffer failed: %d", result);

    DLOG("Command buffer begin");

    VkShaderModule compute_shader_module_;
    VkShaderModuleCreateInfo sh_info = {};
    sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "loop.comp.h"
    sh_info.codeSize = sizeof(loop_main);
    sh_info.pCode = loop_main;
    if ((result = vkCreateShaderModule(vk_device_, &sh_info, NULL, &compute_shader_module_)) !=
        VK_SUCCESS) {
        return DRETF(false, "vkCreateShaderModule failed: %d", result);
    }

    VkPipelineLayout layout;

    VkPipelineLayoutCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr};

    if ((result = vkCreatePipelineLayout(vk_device_, &pipeline_create_info, nullptr, &layout)) !=
        VK_SUCCESS) {
        return DRETF(false, "vkCreatePipelineLayout failed: %d", result);
    }

    VkPipeline compute_pipeline;

    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .pNext = nullptr,
                  .flags = 0,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = compute_shader_module_,
                  .pName = "main",
                  .pSpecializationInfo = nullptr},
        .layout = layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0};

    if ((result = vkCreateComputePipelines(vk_device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                           &compute_pipeline)) != VK_SUCCESS) {
        return DRETF(false, "vkCreateComputePipelines failed: %d", result);
    }

    if (hang_on_event_) {
        VkEvent event;
        VkEventCreateInfo event_info = {
            .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, .pNext = nullptr, .flags = 0};
        if ((result = vkCreateEvent(vk_device_, &event_info, nullptr, &event)) != VK_SUCCESS)
            return DRETF(false, "vkCreateEvent failed: %d", result);

        vkCmdWaitEvents(vk_command_buffer_, 1, &event, VK_PIPELINE_STAGE_HOST_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 0, nullptr);
    } else {
        vkCmdBindPipeline(vk_command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
        vkCmdDispatch(vk_command_buffer_, 1, 1, 1);
    }

    if ((result = vkEndCommandBuffer(vk_command_buffer_)) != VK_SUCCESS)
        return DRETF(false, "vkEndCommandBuffer failed: %d", result);

    DLOG("Command buffer end");

    return true;
}

bool VkLoopTest::Exec()
{
    VkResult result;
    result = vkQueueWaitIdle(vk_queue_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueWaitIdle failed with result %d", result);

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

    if ((result = vkQueueSubmit(vk_queue_, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed");

    for (int i = 0; i < 5; i++) {
        result = vkQueueWaitIdle(vk_queue_);
        if (result != VK_SUCCESS)
            break;
    }
    if (result != VK_ERROR_DEVICE_LOST)
        return DRETF(false, "Result was %d instead of VK_ERROR_DEVICE_LOST", result);

    return true;
}

TEST(Vulkan, InfiniteLoop)
{
    for (int i = 0; i < 2; i++) {
        VkLoopTest test(false);
        ASSERT_TRUE(test.Initialize());
        ASSERT_TRUE(test.Exec());
    }
}

TEST(Vulkan, EventHang)
{
    VkLoopTest test(true);
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec());
}

} // namespace
