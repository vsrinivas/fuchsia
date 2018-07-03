// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

#include <chrono>
#include <thread>
#include <vector>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace {

class VkPriorityTest {
public:
    VkPriorityTest() {}

    bool Initialize();
    bool Exec();

private:
    bool InitVulkan();
    bool InitCommandPool();
    bool InitCommandBuffer(VkCommandBuffer* command_buffer, uint32_t executions);

    bool is_initialized_ = false;
    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue low_prio_vk_queue_;
    VkQueue high_prio_vk_queue_;

    VkCommandPool vk_command_pool_;
    VkCommandBuffer low_prio_vk_command_buffer_;
    VkCommandBuffer high_prio_vk_command_buffer_;
};

bool VkPriorityTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    if (!InitCommandPool())
        return DRETF(false, "InitCommandPool failed");

    if (!InitCommandBuffer(&low_prio_vk_command_buffer_, 1000))
        return DRETF(false, "InitImage failed");

    if (!InitCommandBuffer(&high_prio_vk_command_buffer_, 1))
        return DRETF(false, "InitImage failed");

    is_initialized_ = true;

    return true;
}

bool VkPriorityTest::InitVulkan()
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

    if (queue_family_properties[queue_family_index].queueCount < 2)
        return DRETF(false, "Need 2 queues to use priorities");

    float queue_priorities[2] = {0.0, 1.0};

    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .queueFamilyIndex = 0,
                                                 .queueCount = 2,
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

    vkGetDeviceQueue(vkdevice, queue_family_index, 0, &low_prio_vk_queue_);
    vkGetDeviceQueue(vkdevice, queue_family_index, 1, &high_prio_vk_queue_);

    return true;
}

bool VkPriorityTest::InitCommandPool()
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
    return true;
}

bool VkPriorityTest::InitCommandBuffer(VkCommandBuffer* command_buffer, uint32_t executions)
{
    VkCommandBufferAllocateInfo command_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = vk_command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VkResult result;
    if ((result = vkAllocateCommandBuffers(vk_device_, &command_buffer_create_info,
                                           command_buffer)) != VK_SUCCESS)
        return DRETF(false, "vkAllocateCommandBuffers failed: %d", result);

    DLOG("Created command buffer");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr, // ignored for primary buffers
    };
    if ((result = vkBeginCommandBuffer(*command_buffer, &begin_info)) != VK_SUCCESS)
        return DRETF(false, "vkBeginCommandBuffer failed: %d", result);

    DLOG("Command buffer begin");

    VkShaderModule compute_shader_module_;
    VkShaderModuleCreateInfo sh_info = {};
    sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "priority.comp.h"
    sh_info.codeSize = sizeof(priority_comp);
    sh_info.pCode = priority_comp;
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

    vkCmdBindPipeline(*command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdDispatch(*command_buffer, 1000, executions, 10);

    if ((result = vkEndCommandBuffer(*command_buffer)) != VK_SUCCESS)
        return DRETF(false, "vkEndCommandBuffer failed: %d", result);

    DLOG("Command buffer end");

    return true;
}

bool VkPriorityTest::Exec()
{
    VkResult result;
    result = vkQueueWaitIdle(low_prio_vk_queue_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueWaitIdle failed with result %d", result);
    result = vkQueueWaitIdle(high_prio_vk_queue_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueWaitIdle failed with result %d", result);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &low_prio_vk_command_buffer_,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    auto low_prio_start_time = std::chrono::steady_clock::now();
    if ((result = vkQueueSubmit(low_prio_vk_queue_, 1, &submit_info, VK_NULL_HANDLE)) != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed: %d", result);
    // Should be enough time for the first queue to start executing.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    VkSubmitInfo high_prio_submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &high_prio_vk_command_buffer_,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    auto high_prio_start_time = std::chrono::steady_clock::now();
    if ((result = vkQueueSubmit(high_prio_vk_queue_, 1, &high_prio_submit_info, VK_NULL_HANDLE)) !=
        VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed: %d", result);
    if ((result = vkQueueWaitIdle(high_prio_vk_queue_)) != VK_SUCCESS) {
        return DRETF(false, "vkQueueWaitIdle failed: %d", result);
    }
    auto high_prio_end_time = std::chrono::steady_clock::now();
    auto high_prio_duration = high_prio_end_time - high_prio_start_time;
    printf("first vkQueueWaitIdle finished duration: %lld\n",
           std::chrono::duration_cast<std::chrono::milliseconds>(high_prio_duration).count());

    if ((result = vkQueueWaitIdle(low_prio_vk_queue_)) != VK_SUCCESS) {
        return DRETF(false, "vkQueueWaitIdle failed: %d", result);
    }
    auto low_prio_end_time = std::chrono::steady_clock::now();
    auto low_prio_duration = low_prio_end_time - low_prio_start_time;
    printf("second vkQueueWaitIdle finished duration: %lld\n",
           std::chrono::duration_cast<std::chrono::milliseconds>(low_prio_duration).count());

    // Depends on the precise scheduling, so may sometimes fail.
    EXPECT_LE(high_prio_duration, low_prio_duration / 10);

    return true;
}

TEST(Vulkan, Priority)
{
    VkPriorityTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec());
}

} // namespace
