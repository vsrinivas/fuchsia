// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_shim.h"
#include <assert.h>
#include <stdio.h>

//#define LOG(...) printf(__VA_ARGS__)
#ifndef LOG
#define LOG(...)
#endif

extern "C" {
// Exposed statically by the vulkan implementation.
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
}

static PFN_vkVoidFunction vulkan_shim_get_proc_addr(VkInstance instance, const char* name)
{
    PFN_vkVoidFunction address = vk_icdGetInstanceProcAddr(instance, name);
    if (!address)
        LOG("vk_icdGetInstanceProcAddr failed for name %s\n", name);

    return address;
}

VkResult VulkanShimInit()
{
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        vulkan_shim_get_proc_addr(nullptr, "vkCreateInstance"));

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkInstance instance;
    VkResult result = vkCreateInstance(&create_info, nullptr, &instance);
    if (result != VK_SUCCESS)
        return result;

    vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyInstance"));
    vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        vulkan_shim_get_proc_addr(instance, "vkEnumeratePhysicalDevices"));
    vkGetPhysicalDeviceFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
        vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceFeatures"));
    vkGetPhysicalDeviceFormatProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceFormatProperties"));
    vkGetPhysicalDeviceImageFormatProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceImageFormatProperties"));
    vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceProperties"));
    vkGetPhysicalDeviceQueueFamilyProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceMemoryProperties"));
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        vulkan_shim_get_proc_addr(instance, "vkGetInstanceProcAddr"));
    vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vulkan_shim_get_proc_addr(instance, "vkGetDeviceProcAddr"));
    vkCreateDevice =
        reinterpret_cast<PFN_vkCreateDevice>(vulkan_shim_get_proc_addr(instance, "vkCreateDevice"));
    vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyDevice"));
    vkEnumerateInstanceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            vulkan_shim_get_proc_addr(instance, "vkEnumerateInstanceExtensionProperties"));
    vkEnumerateDeviceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            vulkan_shim_get_proc_addr(instance, "vkEnumerateDeviceExtensionProperties"));
    vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
        vulkan_shim_get_proc_addr(instance, "vkEnumerateInstanceLayerProperties"));
    vkEnumerateDeviceLayerProperties = reinterpret_cast<PFN_vkEnumerateDeviceLayerProperties>(
        vulkan_shim_get_proc_addr(instance, "vkEnumerateDeviceLayerProperties"));
    vkGetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(
        vulkan_shim_get_proc_addr(instance, "vkGetDeviceQueue"));
    vkQueueSubmit =
        reinterpret_cast<PFN_vkQueueSubmit>(vulkan_shim_get_proc_addr(instance, "vkQueueSubmit"));
    vkQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(
        vulkan_shim_get_proc_addr(instance, "vkQueueWaitIdle"));
    vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
        vulkan_shim_get_proc_addr(instance, "vkDeviceWaitIdle"));
    vkAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(
        vulkan_shim_get_proc_addr(instance, "vkAllocateMemory"));
    vkFreeMemory =
        reinterpret_cast<PFN_vkFreeMemory>(vulkan_shim_get_proc_addr(instance, "vkFreeMemory"));
    vkMapMemory =
        reinterpret_cast<PFN_vkMapMemory>(vulkan_shim_get_proc_addr(instance, "vkMapMemory"));
    vkUnmapMemory =
        reinterpret_cast<PFN_vkUnmapMemory>(vulkan_shim_get_proc_addr(instance, "vkUnmapMemory"));
    vkFlushMappedMemoryRanges = reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(
        vulkan_shim_get_proc_addr(instance, "vkFlushMappedMemoryRanges"));
    vkInvalidateMappedMemoryRanges = reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(
        vulkan_shim_get_proc_addr(instance, "vkInvalidateMappedMemoryRanges"));
    vkGetDeviceMemoryCommitment = reinterpret_cast<PFN_vkGetDeviceMemoryCommitment>(
        vulkan_shim_get_proc_addr(instance, "vkGetDeviceMemoryCommitment"));
    vkBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(
        vulkan_shim_get_proc_addr(instance, "vkBindBufferMemory"));
    vkBindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(
        vulkan_shim_get_proc_addr(instance, "vkBindImageMemory"));
    vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
        vulkan_shim_get_proc_addr(instance, "vkGetBufferMemoryRequirements"));
    vkGetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(
        vulkan_shim_get_proc_addr(instance, "vkGetImageMemoryRequirements"));
    vkGetImageSparseMemoryRequirements = reinterpret_cast<PFN_vkGetImageSparseMemoryRequirements>(
        vulkan_shim_get_proc_addr(instance, "vkGetImageSparseMemoryRequirements"));
    vkGetPhysicalDeviceSparseImageFormatProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSparseImageFormatProperties>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceSparseImageFormatProperties"));
    vkQueueBindSparse = reinterpret_cast<PFN_vkQueueBindSparse>(
        vulkan_shim_get_proc_addr(instance, "vkQueueBindSparse"));
    vkCreateFence =
        reinterpret_cast<PFN_vkCreateFence>(vulkan_shim_get_proc_addr(instance, "vkCreateFence"));
    vkDestroyFence =
        reinterpret_cast<PFN_vkDestroyFence>(vulkan_shim_get_proc_addr(instance, "vkDestroyFence"));
    vkResetFences =
        reinterpret_cast<PFN_vkResetFences>(vulkan_shim_get_proc_addr(instance, "vkResetFences"));
    vkGetFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(
        vulkan_shim_get_proc_addr(instance, "vkGetFenceStatus"));
    vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(
        vulkan_shim_get_proc_addr(instance, "vkWaitForFences"));
    vkCreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(
        vulkan_shim_get_proc_addr(instance, "vkCreateSemaphore"));
    vkDestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(
        vulkan_shim_get_proc_addr(instance, "vkDestroySemaphore"));
    vkCreateEvent =
        reinterpret_cast<PFN_vkCreateEvent>(vulkan_shim_get_proc_addr(instance, "vkCreateEvent"));
    vkDestroyEvent =
        reinterpret_cast<PFN_vkDestroyEvent>(vulkan_shim_get_proc_addr(instance, "vkDestroyEvent"));
    vkGetEventStatus = reinterpret_cast<PFN_vkGetEventStatus>(
        vulkan_shim_get_proc_addr(instance, "vkGetEventStatus"));
    vkSetEvent =
        reinterpret_cast<PFN_vkSetEvent>(vulkan_shim_get_proc_addr(instance, "vkSetEvent"));
    vkResetEvent =
        reinterpret_cast<PFN_vkResetEvent>(vulkan_shim_get_proc_addr(instance, "vkResetEvent"));
    vkCreateQueryPool = reinterpret_cast<PFN_vkCreateQueryPool>(
        vulkan_shim_get_proc_addr(instance, "vkCreateQueryPool"));
    vkDestroyQueryPool = reinterpret_cast<PFN_vkDestroyQueryPool>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyQueryPool"));
    vkGetQueryPoolResults = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        vulkan_shim_get_proc_addr(instance, "vkGetQueryPoolResults"));
    vkCreateBuffer =
        reinterpret_cast<PFN_vkCreateBuffer>(vulkan_shim_get_proc_addr(instance, "vkCreateBuffer"));
    vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyBuffer"));
    vkCreateBufferView = reinterpret_cast<PFN_vkCreateBufferView>(
        vulkan_shim_get_proc_addr(instance, "vkCreateBufferView"));
    vkDestroyBufferView = reinterpret_cast<PFN_vkDestroyBufferView>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyBufferView"));
    vkCreateImage =
        reinterpret_cast<PFN_vkCreateImage>(vulkan_shim_get_proc_addr(instance, "vkCreateImage"));
    vkDestroyImage =
        reinterpret_cast<PFN_vkDestroyImage>(vulkan_shim_get_proc_addr(instance, "vkDestroyImage"));
    vkGetImageSubresourceLayout = reinterpret_cast<PFN_vkGetImageSubresourceLayout>(
        vulkan_shim_get_proc_addr(instance, "vkGetImageSubresourceLayout"));
    vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(
        vulkan_shim_get_proc_addr(instance, "vkCreateImageView"));
    vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyImageView"));
    vkCreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(
        vulkan_shim_get_proc_addr(instance, "vkCreateShaderModule"));
    vkDestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyShaderModule"));
    vkCreatePipelineCache = reinterpret_cast<PFN_vkCreatePipelineCache>(
        vulkan_shim_get_proc_addr(instance, "vkCreatePipelineCache"));
    vkDestroyPipelineCache = reinterpret_cast<PFN_vkDestroyPipelineCache>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyPipelineCache"));
    vkGetPipelineCacheData = reinterpret_cast<PFN_vkGetPipelineCacheData>(
        vulkan_shim_get_proc_addr(instance, "vkGetPipelineCacheData"));
    vkMergePipelineCaches = reinterpret_cast<PFN_vkMergePipelineCaches>(
        vulkan_shim_get_proc_addr(instance, "vkMergePipelineCaches"));
    vkCreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(
        vulkan_shim_get_proc_addr(instance, "vkCreateGraphicsPipelines"));
    vkCreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(
        vulkan_shim_get_proc_addr(instance, "vkCreateComputePipelines"));
    vkDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyPipeline"));
    vkCreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
        vulkan_shim_get_proc_addr(instance, "vkCreatePipelineLayout"));
    vkDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyPipelineLayout"));
    vkCreateSampler = reinterpret_cast<PFN_vkCreateSampler>(
        vulkan_shim_get_proc_addr(instance, "vkCreateSampler"));
    vkDestroySampler = reinterpret_cast<PFN_vkDestroySampler>(
        vulkan_shim_get_proc_addr(instance, "vkDestroySampler"));
    vkCreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
        vulkan_shim_get_proc_addr(instance, "vkCreateDescriptorSetLayout"));
    vkDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyDescriptorSetLayout"));
    vkCreateDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(
        vulkan_shim_get_proc_addr(instance, "vkCreateDescriptorPool"));
    vkDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyDescriptorPool"));
    vkResetDescriptorPool = reinterpret_cast<PFN_vkResetDescriptorPool>(
        vulkan_shim_get_proc_addr(instance, "vkResetDescriptorPool"));
    vkAllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(
        vulkan_shim_get_proc_addr(instance, "vkAllocateDescriptorSets"));
    vkFreeDescriptorSets = reinterpret_cast<PFN_vkFreeDescriptorSets>(
        vulkan_shim_get_proc_addr(instance, "vkFreeDescriptorSets"));
    vkUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(
        vulkan_shim_get_proc_addr(instance, "vkUpdateDescriptorSets"));
    vkCreateFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCreateFramebuffer"));
    vkDestroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyFramebuffer"));
    vkCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(
        vulkan_shim_get_proc_addr(instance, "vkCreateRenderPass"));
    vkDestroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyRenderPass"));
    vkGetRenderAreaGranularity = reinterpret_cast<PFN_vkGetRenderAreaGranularity>(
        vulkan_shim_get_proc_addr(instance, "vkGetRenderAreaGranularity"));
    vkCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(
        vulkan_shim_get_proc_addr(instance, "vkCreateCommandPool"));
    vkDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(
        vulkan_shim_get_proc_addr(instance, "vkDestroyCommandPool"));
    vkResetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(
        vulkan_shim_get_proc_addr(instance, "vkResetCommandPool"));
    vkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
        vulkan_shim_get_proc_addr(instance, "vkAllocateCommandBuffers"));
    vkFreeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(
        vulkan_shim_get_proc_addr(instance, "vkFreeCommandBuffers"));
    vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkBeginCommandBuffer"));
    vkEndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkEndCommandBuffer"));
    vkResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkResetCommandBuffer"));
    vkCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBindPipeline"));
    vkCmdSetViewport = reinterpret_cast<PFN_vkCmdSetViewport>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetViewport"));
    vkCmdSetScissor = reinterpret_cast<PFN_vkCmdSetScissor>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetScissor"));
    vkCmdSetLineWidth = reinterpret_cast<PFN_vkCmdSetLineWidth>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetLineWidth"));
    vkCmdSetDepthBias = reinterpret_cast<PFN_vkCmdSetDepthBias>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetDepthBias"));
    vkCmdSetBlendConstants = reinterpret_cast<PFN_vkCmdSetBlendConstants>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetBlendConstants"));
    vkCmdSetDepthBounds = reinterpret_cast<PFN_vkCmdSetDepthBounds>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetDepthBounds"));
    vkCmdSetStencilCompareMask = reinterpret_cast<PFN_vkCmdSetStencilCompareMask>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetStencilCompareMask"));
    vkCmdSetStencilWriteMask = reinterpret_cast<PFN_vkCmdSetStencilWriteMask>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetStencilWriteMask"));
    vkCmdSetStencilReference = reinterpret_cast<PFN_vkCmdSetStencilReference>(
        vulkan_shim_get_proc_addr(instance, "vkCmdSetStencilReference"));
    vkCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBindDescriptorSets"));
    vkCmdBindIndexBuffer = reinterpret_cast<PFN_vkCmdBindIndexBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBindIndexBuffer"));
    vkCmdBindVertexBuffers = reinterpret_cast<PFN_vkCmdBindVertexBuffers>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBindVertexBuffers"));
    vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(vulkan_shim_get_proc_addr(instance, "vkCmdDraw"));
    vkCmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(
        vulkan_shim_get_proc_addr(instance, "vkCmdDrawIndexed"));
    vkCmdDrawIndirect = reinterpret_cast<PFN_vkCmdDrawIndirect>(
        vulkan_shim_get_proc_addr(instance, "vkCmdDrawIndirect"));
    vkCmdDrawIndexedIndirect = reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(
        vulkan_shim_get_proc_addr(instance, "vkCmdDrawIndexedIndirect"));
    vkCmdDispatch =
        reinterpret_cast<PFN_vkCmdDispatch>(vulkan_shim_get_proc_addr(instance, "vkCmdDispatch"));
    vkCmdDispatchIndirect = reinterpret_cast<PFN_vkCmdDispatchIndirect>(
        vulkan_shim_get_proc_addr(instance, "vkCmdDispatchIndirect"));
    vkCmdCopyBuffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCmdCopyBuffer"));
    vkCmdCopyImage =
        reinterpret_cast<PFN_vkCmdCopyImage>(vulkan_shim_get_proc_addr(instance, "vkCmdCopyImage"));
    vkCmdBlitImage =
        reinterpret_cast<PFN_vkCmdBlitImage>(vulkan_shim_get_proc_addr(instance, "vkCmdBlitImage"));
    vkCmdCopyBufferToImage = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(
        vulkan_shim_get_proc_addr(instance, "vkCmdCopyBufferToImage"));
    vkCmdCopyImageToBuffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCmdCopyImageToBuffer"));
    vkCmdUpdateBuffer = reinterpret_cast<PFN_vkCmdUpdateBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCmdUpdateBuffer"));
    vkCmdFillBuffer = reinterpret_cast<PFN_vkCmdFillBuffer>(
        vulkan_shim_get_proc_addr(instance, "vkCmdFillBuffer"));
    vkCmdClearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(
        vulkan_shim_get_proc_addr(instance, "vkCmdClearColorImage"));
    vkCmdClearDepthStencilImage = reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(
        vulkan_shim_get_proc_addr(instance, "vkCmdClearDepthStencilImage"));
    vkCmdClearAttachments = reinterpret_cast<PFN_vkCmdClearAttachments>(
        vulkan_shim_get_proc_addr(instance, "vkCmdClearAttachments"));
    vkCmdResolveImage = reinterpret_cast<PFN_vkCmdResolveImage>(
        vulkan_shim_get_proc_addr(instance, "vkCmdResolveImage"));
    vkCmdSetEvent =
        reinterpret_cast<PFN_vkCmdSetEvent>(vulkan_shim_get_proc_addr(instance, "vkCmdSetEvent"));
    vkCmdResetEvent = reinterpret_cast<PFN_vkCmdResetEvent>(
        vulkan_shim_get_proc_addr(instance, "vkCmdResetEvent"));
    vkCmdWaitEvents = reinterpret_cast<PFN_vkCmdWaitEvents>(
        vulkan_shim_get_proc_addr(instance, "vkCmdWaitEvents"));
    vkCmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
        vulkan_shim_get_proc_addr(instance, "vkCmdPipelineBarrier"));
    vkCmdBeginQuery = reinterpret_cast<PFN_vkCmdBeginQuery>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBeginQuery"));
    vkCmdEndQuery =
        reinterpret_cast<PFN_vkCmdEndQuery>(vulkan_shim_get_proc_addr(instance, "vkCmdEndQuery"));
    vkCmdResetQueryPool = reinterpret_cast<PFN_vkCmdResetQueryPool>(
        vulkan_shim_get_proc_addr(instance, "vkCmdResetQueryPool"));
    vkCmdWriteTimestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        vulkan_shim_get_proc_addr(instance, "vkCmdWriteTimestamp"));
    vkCmdCopyQueryPoolResults = reinterpret_cast<PFN_vkCmdCopyQueryPoolResults>(
        vulkan_shim_get_proc_addr(instance, "vkCmdCopyQueryPoolResults"));
    vkCmdPushConstants = reinterpret_cast<PFN_vkCmdPushConstants>(
        vulkan_shim_get_proc_addr(instance, "vkCmdPushConstants"));
    vkCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
        vulkan_shim_get_proc_addr(instance, "vkCmdBeginRenderPass"));
    vkCmdNextSubpass = reinterpret_cast<PFN_vkCmdNextSubpass>(
        vulkan_shim_get_proc_addr(instance, "vkCmdNextSubpass"));
    vkCmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(
        vulkan_shim_get_proc_addr(instance, "vkCmdEndRenderPass"));
    vkCmdExecuteCommands = reinterpret_cast<PFN_vkCmdExecuteCommands>(
        vulkan_shim_get_proc_addr(instance, "vkCmdExecuteCommands"));
    vkDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkDestroySurfaceKHR"));
    vkGetPhysicalDeviceSurfaceSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    vkGetPhysicalDeviceSurfaceFormatsKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    vkGetPhysicalDeviceSurfacePresentModesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateSwapchainKHR"));
    vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        vulkan_shim_get_proc_addr(instance, "vkDestroySwapchainKHR"));
    vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        vulkan_shim_get_proc_addr(instance, "vkGetSwapchainImagesKHR"));
    vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        vulkan_shim_get_proc_addr(instance, "vkAcquireNextImageKHR"));
    vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(
        vulkan_shim_get_proc_addr(instance, "vkQueuePresentKHR"));
    vkGetPhysicalDeviceDisplayPropertiesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPropertiesKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceDisplayPropertiesKHR"));
    vkGetPhysicalDeviceDisplayPlanePropertiesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR"));
    vkGetDisplayPlaneSupportedDisplaysKHR =
        reinterpret_cast<PFN_vkGetDisplayPlaneSupportedDisplaysKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetDisplayPlaneSupportedDisplaysKHR"));
    vkGetDisplayModePropertiesKHR = reinterpret_cast<PFN_vkGetDisplayModePropertiesKHR>(
        vulkan_shim_get_proc_addr(instance, "vkGetDisplayModePropertiesKHR"));
    vkCreateDisplayModeKHR = reinterpret_cast<PFN_vkCreateDisplayModeKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateDisplayModeKHR"));
    vkGetDisplayPlaneCapabilitiesKHR = reinterpret_cast<PFN_vkGetDisplayPlaneCapabilitiesKHR>(
        vulkan_shim_get_proc_addr(instance, "vkGetDisplayPlaneCapabilitiesKHR"));
    vkCreateDisplayPlaneSurfaceKHR = reinterpret_cast<PFN_vkCreateDisplayPlaneSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateDisplayPlaneSurfaceKHR"));
    vkCreateSharedSwapchainsKHR = reinterpret_cast<PFN_vkCreateSharedSwapchainsKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateSharedSwapchainsKHR"));

#ifdef VK_USE_PLATFORM_XLIB_KHR
    vkCreateXlibSurfaceKHR = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateXlibSurfaceKHR"));
    vkGetPhysicalDeviceXlibPresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceXlibPresentationSupportKHR"));
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
    vkCreateXcbSurfaceKHR = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateXcbSurfaceKHR"));
    vkGetPhysicalDeviceXcbPresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceXcbPresentationSupportKHR"));
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    vkCreateWaylandSurfaceKHR = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateWaylandSurfaceKHR"));
    vkGetPhysicalDeviceWaylandPresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance,
                                      "vkGetPhysicalDeviceWaylandPresentationSupportKHR"));
#endif

#ifdef VK_USE_PLATFORM_MIR_KHR
    vkCreateMirSurfaceKHR = reinterpret_cast<PFN_vkCreateMirSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateMirSurfaceKHR"));
    vkGetPhysicalDeviceMirPresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMirPresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceMirPresentationSupportKHR"));
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
    vkCreateAndroidSurfaceKHR = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateAndroidSurfaceKHR"));
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
    vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateWin32SurfaceKHR"));
    vkGetPhysicalDeviceWin32PresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
#endif

#ifdef VK_USE_PLATFORM_MAGMA_KHR
    vkCreateMagmaSurfaceKHR = reinterpret_cast<PFN_vkCreateMagmaSurfaceKHR>(
        vulkan_shim_get_proc_addr(instance, "vkCreateMagmaSurfaceKHR"));
    vkGetPhysicalDeviceMagmaPresentationSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMagmaPresentationSupportKHR>(
            vulkan_shim_get_proc_addr(instance, "vkGetPhysicalDeviceMagmaPresentationSupportKHR"));
#endif

    vkDestroyInstance(instance, nullptr);

    return VK_SUCCESS;
}

// No Vulkan support, do not set function addresses
PFN_vkCreateInstance vkCreateInstance;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkCreateDevice vkCreateDevice;
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueueWaitIdle vkQueueWaitIdle;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkMapMemory vkMapMemory;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
PFN_vkQueueBindSparse vkQueueBindSparse;
PFN_vkCreateFence vkCreateFence;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkResetFences vkResetFences;
PFN_vkGetFenceStatus vkGetFenceStatus;
PFN_vkWaitForFences vkWaitForFences;
PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkCreateEvent vkCreateEvent;
PFN_vkDestroyEvent vkDestroyEvent;
PFN_vkGetEventStatus vkGetEventStatus;
PFN_vkSetEvent vkSetEvent;
PFN_vkResetEvent vkResetEvent;
PFN_vkCreateQueryPool vkCreateQueryPool;
PFN_vkDestroyQueryPool vkDestroyQueryPool;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkCreateBufferView vkCreateBufferView;
PFN_vkDestroyBufferView vkDestroyBufferView;
PFN_vkCreateImage vkCreateImage;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyShaderModule vkDestroyShaderModule;
PFN_vkCreatePipelineCache vkCreatePipelineCache;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
PFN_vkMergePipelineCaches vkMergePipelineCaches;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkCreateComputePipelines vkCreateComputePipelines;
PFN_vkDestroyPipeline vkDestroyPipeline;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkCreateSampler vkCreateSampler;
PFN_vkDestroySampler vkDestroySampler;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
PFN_vkResetDescriptorPool vkResetDescriptorPool;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkDestroyRenderPass vkDestroyRenderPass;
PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
PFN_vkCmdDispatch vkCmdDispatch;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdBlitImage vkCmdBlitImage;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
PFN_vkCmdFillBuffer vkCmdFillBuffer;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
PFN_vkCmdResolveImage vkCmdResolveImage;
PFN_vkCmdSetEvent vkCmdSetEvent;
PFN_vkCmdResetEvent vkCmdResetEvent;
PFN_vkCmdWaitEvents vkCmdWaitEvents;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdBeginQuery vkCmdBeginQuery;
PFN_vkCmdEndQuery vkCmdEndQuery;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
PFN_vkCmdPushConstants vkCmdPushConstants;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdNextSubpass vkCmdNextSubpass;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR vkQueuePresentKHR;
PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR;
PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
PFN_vkCreateDisplayModeKHR vkCreateDisplayModeKHR;
PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
PFN_vkCreateSharedSwapchainsKHR vkCreateSharedSwapchainsKHR;

#ifdef VK_USE_PLATFORM_XLIB_KHR
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR vkGetPhysicalDeviceXlibPresentationSupportKHR;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR vkGetPhysicalDeviceXcbPresentationSupportKHR;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR
    vkGetPhysicalDeviceWaylandPresentationSupportKHR;
#endif

#ifdef VK_USE_PLATFORM_MIR_KHR
PFN_vkCreateMirSurfaceKHR vkCreateMirSurfaceKHR;
PFN_vkGetPhysicalDeviceMirPresentationSupportKHR vkGetPhysicalDeviceMirPresentationSupportKHR;
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR vkGetPhysicalDeviceWin32PresentationSupportKHR;
#endif

#ifdef VK_USE_PLATFORM_MAGMA_KHR
PFN_vkCreateMagmaSurfaceKHR vkCreateMagmaSurfaceKHR;
PFN_vkGetPhysicalDeviceMagmaPresentationSupportKHR vkGetPhysicalDeviceMagmaPresentationSupportKHR;
#endif

PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;
PFN_vkDebugReportMessageEXT vkDebugReportMessageEXT;
