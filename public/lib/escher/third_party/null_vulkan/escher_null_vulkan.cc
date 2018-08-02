/*
** Copyright (c) 2015-2017 The Khronos Group Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance) { return VK_ERROR_INCOMPATIBLE_DRIVER; }

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices) { return VK_ERROR_INITIALIZATION_FAILED; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties) {}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName) {
  return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName) { return NULL; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice) { return VK_ERROR_INITIALIZATION_FAILED; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) {}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue                                     queue) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(
    VkDevice                                    device) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory) {}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes) {}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements) {}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements) {}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkSampleCountFlagBits                       samples,
    VkImageUsageFlags                           usage,
    VkImageTiling                               tiling,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties*              pProperties) {}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(
    VkQueue                                     queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(
    VkDevice                                    device,
    VkFence                                     fence) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(
    VkDevice                                    device,
    const VkEventCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkEvent*                                    pEvent) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice                                    device,
    VkEvent                                     event,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(
    VkDevice                                    device,
    VkEvent                                     event) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(
    VkDevice                                    device,
    VkEvent                                     event) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(
    VkDevice                                    device,
    VkEvent                                     event) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice                                    device,
    const VkBufferViewCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBufferView*                               pView) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(
    VkDevice                                    device,
    VkBufferView                                bufferView,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource*                   pSubresource,
    VkSubresourceLayout*                        pLayout) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    size_t*                                     pDataSize,
    void*                                       pData) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice                                    device,
    VkPipelineCache                             dstCache,
    uint32_t                                    srcCacheCount,
    const VkPipelineCache*                      pSrcCaches) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice                                    device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice                                    device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers) {}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer                             commandBuffer) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(
    VkCommandBuffer                             commandBuffer,
    float                                       lineWidth) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(
    VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4]) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(
    VkCommandBuffer                             commandBuffer,
    float                                       minDepthBounds,
    float                                       maxDepthBounds) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset) {}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions) {}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageCopy*                          pRegions) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter) {}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                dataSize,
    const void*                                 pData) {}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data) {}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects) {}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageResolve*                       pRegions) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask) {}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask) {}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) {}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags) {}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) {}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount) {}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) {}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags) {}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues) {}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents) {}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents) {}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(
    VkCommandBuffer                             commandBuffer) {}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers) {}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPropertiesKHR*                     pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPlanePropertiesKHR*                pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    planeIndex,
    uint32_t*                                   pDisplayCount,
    VkDisplayKHR*                               pDisplays) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModePropertiesKHR*                 pProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    const VkDisplayModeCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDisplayModeKHR*                           pMode) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayModeKHR                            mode,
    uint32_t                                    planeIndex,
    VkDisplayPlaneCapabilitiesKHR*              pCapabilities) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(
    VkInstance                                  instance,
    const VkDisplaySurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

#ifdef VK_USE_PLATFORM_XLIB_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance                                  instance,
    const VkXlibSurfaceCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    Display*                                    dpy,
    VisualID                                    visualID) { return VK_TRUE; }

#endif /* VK_USE_PLATFORM_XLIB_KHR */

#ifdef VK_USE_PLATFORM_XCB_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance                                  instance,
    const VkXcbSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id) { return VK_TRUE; }
#endif /* VK_USE_PLATFORM_XCB_KHR */

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(
    VkInstance                                  instance,
    const VkWaylandSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    struct wl_display*                          display) { return VK_TRUE; }

#endif /* VK_USE_PLATFORM_WAYLAND_KHR */

#ifdef VK_USE_PLATFORM_MIR_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMirSurfaceKHR(
    VkInstance                                  instance,
    const VkMirSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    MirConnection*                              connection) { return VK_TRUE; }

#endif /* VK_USE_PLATFORM_MIR_KHR */

#ifdef VK_USE_PLATFORM_ANDROID_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(
    VkInstance                                  instance,
    const VkAndroidSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_ANDROID_KHR */

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance                                  instance,
    const VkWin32SurfaceCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex) { return VK_TRUE; }

#endif /* VK_USE_PLATFORM_WIN32_KHR */

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2KHR*               pFeatures) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2KHR*             pProperties) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2KHR*                     pFormatProperties) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2KHR*  pImageFormatInfo,
    VkImageFormatProperties2KHR*                pImageFormatProperties) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR*                pQueueFamilyProperties) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR*       pMemoryProperties) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2KHR*          pProperties) {}

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPoolKHR(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlagsKHR                   flags) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo,
    VkExternalBufferPropertiesKHR*              pExternalBufferProperties) {}

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandleKHR(
    VkDevice                                    device,
    const VkMemoryGetWin32HandleInfoKHR*        pGetWin32HandleInfo,
    HANDLE*                                     pHandle) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandlePropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    HANDLE                                      handle,
    VkMemoryWin32HandlePropertiesKHR*           pMemoryWin32HandleProperties) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_WIN32_KHR */

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(
    VkDevice                                    device,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdPropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo,
    VkExternalSemaphorePropertiesKHR*           pExternalSemaphoreProperties) {}

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    const VkImportSemaphoreWin32HandleInfoKHR*  pImportSemaphoreWin32HandleInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    const VkSemaphoreGetWin32HandleInfoKHR*     pGetWin32HandleInfo,
    HANDLE*                                     pHandle) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_WIN32_KHR */

VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFdKHR(
    VkDevice                                    device,
    const VkImportSemaphoreFdInfoKHR*           pImportSemaphoreFdInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFdKHR(
    VkDevice                                    device,
    const VkSemaphoreGetFdInfoKHR*              pGetFdInfo,
    int*                                        pFd) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetKHR(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplateKHR*              pDescriptorUpdateTemplate) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const void*                                 pData) {}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainStatusKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFencePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo,
    VkExternalFencePropertiesKHR*               pExternalFenceProperties) {}

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceWin32HandleKHR(
    VkDevice                                    device,
    const VkImportFenceWin32HandleInfoKHR*      pImportFenceWin32HandleInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceWin32HandleKHR(
    VkDevice                                    device,
    const VkFenceGetWin32HandleInfoKHR*         pGetWin32HandleInfo,
    HANDLE*                                     pHandle) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_WIN32_KHR */

VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceFdKHR(
    VkDevice                                    device,
    const VkImportFenceFdInfoKHR*               pImportFenceFdInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(
    VkDevice                                    device,
    const VkFenceGetFdInfoKHR*                  pGetFdInfo,
    int*                                        pFd) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormat2KHR*                        pSurfaceFormats) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2KHR*    pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements) {}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2KHR*   pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements) {}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2KHR* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2KHR*        pSparseMemoryRequirements) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversionKHR(
    VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversionKHR*                pYcbcrConversion) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversionKHR(
    VkDevice                                    device,
    VkSamplerYcbcrConversionKHR                 ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfoKHR*            pBindInfos) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfoKHR*             pBindInfos) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFuchsiaHandleKHR(
    VkDevice                                    device,
    const VkMemoryGetFuchsiaHandleInfoKHR*      pGetFuchsiaHandleInfo,
    uint32_t*                                   pFuchsiaHandle) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFuchsiaHandlePropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    uint32_t                                    fuchsiaHandle,
    VkMemoryFuchsiaHandlePropertiesKHR*         pMemoryFuchsiaHandleProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFuchsiaHandleKHR(
    VkDevice                                    device,
    const VkImportSemaphoreFuchsiaHandleInfoKHR* pImportSemaphoreFuchsiaHandleInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFuchsiaHandleKHR(
    VkDevice                                    device,
    const VkSemaphoreGetFuchsiaHandleInfoKHR*   pGetFuchsiaHandleInfo,
    uint32_t*                                   pFuchsiaHandle) { return VK_SUCCESS; }

#ifdef VK_USE_PLATFORM_MAGMA_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMagmaSurfaceKHR(
    VkInstance                                  instance,
    const VkMagmaSurfaceCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceMagmaPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex);

#endif /* VK_USE_PLATFORM_MAGMA_KHR */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugReportCallbackEXT(
    VkInstance                                  instance,
    const VkDebugReportCallbackCreateInfoEXT*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDebugReportCallbackEXT*                   pCallback) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugReportCallbackEXT(
    VkInstance                                  instance,
    VkDebugReportCallbackEXT                    callback,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR void VKAPI_CALL vkDebugReportMessageEXT(
    VkInstance                                  instance,
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage) {}

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectTagInfoEXT*        pTagInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectNameInfoEXT*       pNameInfo) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(
    VkCommandBuffer                             commandBuffer) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountAMD(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountAMD(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInfoAMD(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    VkShaderStageFlagBits                       shaderStage,
    VkShaderInfoTypeAMD                         infoType,
    size_t*                                     pInfoSize,
    void*                                       pInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkExternalMemoryHandleTypeFlagsNV           externalHandleType,
    VkExternalImageFormatPropertiesNV*          pExternalImageFormatProperties) { return VK_SUCCESS; }

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandleNV(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkExternalMemoryHandleTypeFlagsNV           handleType,
    HANDLE*                                     pHandle) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_WIN32_KHR */

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeaturesKHX(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlagsKHX*                pPeerMemoryFeatures) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMaskKHX(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask) {}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBaseKHX(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) {}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHX(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHX*        pDeviceGroupPresentCapabilities) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHX(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHX*           pModes) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHX(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHX(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHX*            pAcquireInfo,
    uint32_t*                                   pImageIndex) { return VK_SUCCESS; }

#ifdef VK_USE_PLATFORM_VI_NN

VKAPI_ATTR VkResult VKAPI_CALL vkCreateViSurfaceNN(
    VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_VI_NN */

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroupsKHX(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHX*         pPhysicalDeviceGroupProperties) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdProcessCommandsNVX(
    VkCommandBuffer                             commandBuffer,
    const VkCmdProcessCommandsInfoNVX*          pProcessCommandsInfo) {}

VKAPI_ATTR void VKAPI_CALL vkCmdReserveSpaceForCommandsNVX(
    VkCommandBuffer                             commandBuffer,
    const VkCmdReserveSpaceForCommandsInfoNVX*  pReserveSpaceInfo) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutNVX(
    VkDevice                                    device,
    const VkIndirectCommandsLayoutCreateInfoNVX* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutNVX*                pIndirectCommandsLayout) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutNVX(
    VkDevice                                    device,
    VkIndirectCommandsLayoutNVX                 indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateObjectTableNVX(
    VkDevice                                    device,
    const VkObjectTableCreateInfoNVX*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkObjectTableNVX*                           pObjectTable) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyObjectTableNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterObjectsNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    uint32_t                                    objectCount,
    const VkObjectTableEntryNVX* const*         ppObjectTableEntries,
    const uint32_t*                             pObjectIndices) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkUnregisterObjectsNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    uint32_t                                    objectCount,
    const VkObjectEntryTypeNVX*                 pObjectEntryTypes,
    const uint32_t*                             pObjectIndices) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice                            physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX*       pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX*         pLimits) {}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingNV(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewportWScalingNV*                 pViewportWScalings) {}

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display) { return VK_SUCCESS; }

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireXlibDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    VkDisplayKHR                                display) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRandROutputDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    RROutput                                    rrOutput,
    VkDisplayKHR*                               pDisplay) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_XLIB_XRANDR_EXT */

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkDisplayPowerControlEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayPowerInfoEXT*                pDisplayPowerInfo) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDeviceEventEXT(
    VkDevice                                    device,
    const VkDeviceEventInfoEXT*                 pDeviceEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDisplayEventEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayEventInfoEXT*                pDisplayEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t*                                   pCounterValue) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRefreshCycleDurationGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE*               pDisplayTimingProperties) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE*             pPresentationTimings) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstDiscardRectangle,
    uint32_t                                    discardRectangleCount,
    const VkRect2D*                             pDiscardRectangles) {}

VKAPI_ATTR void VKAPI_CALL vkSetHdrMetadataEXT(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR*                       pSwapchains,
    const VkHdrMetadataEXT*                     pMetadata) {}

#ifdef VK_USE_PLATFORM_IOS_MVK

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIOSSurfaceMVK(
    VkInstance                                  instance,
    const VkIOSSurfaceCreateInfoMVK*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_IOS_MVK */

#ifdef VK_USE_PLATFORM_MACOS_MVK

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMacOSSurfaceMVK(
    VkInstance                                  instance,
    const VkMacOSSurfaceCreateInfoMVK*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { return VK_SUCCESS; }

#endif /* VK_USE_PLATFORM_MACOS_MVK */

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEXT(
    VkCommandBuffer                             commandBuffer,
    const VkSampleLocationsInfoEXT*             pSampleLocationsInfo) {}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties) {}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateValidationCacheEXT(
    VkDevice                                    device,
    const VkValidationCacheCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkValidationCacheEXT*                       pValidationCache) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyValidationCacheEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    const VkAllocationCallbacks*                pAllocator) {}

VKAPI_ATTR VkResult VKAPI_CALL vkMergeValidationCachesEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        dstCache,
    uint32_t                                    srcCacheCount,
    const VkValidationCacheEXT*                 pSrcCaches) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetValidationCacheDataEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    size_t*                                     pDataSize,
    void*                                       pData) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryHostPointerPropertiesEXT(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    const void*                                 pHostPointer,
    VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties) { return VK_SUCCESS; }

#ifdef __cplusplus
}
#endif
