// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

static const char* kLayerName = "VK_LAYER_FUCHSIA_compact_image";

// Note: the loader returns results based on the layer's manifest file, not the
// implementation of the vkEnumerateInstanceExtensionProperties and
// vkEnumerateDeviceExtensionProperties apis inside the layer.

static const std::vector<const char*> kLayers = {kLayerName};

static const std::vector<const char*> kExpectedDeviceExtensions = {
    VK_FUCHSIA_COMPACT_IMAGE_EXTENSION_NAME};

TEST(CompactImage, LayerApiVersion) {
  uint32_t prop_count = 0;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&prop_count, nullptr));
  EXPECT_GE(prop_count, kLayers.size());

  std::vector<VkLayerProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&prop_count, props.data()));
  bool layer_found = false;
  const uint32_t kExpectedVersion = VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION);
  for (uint32_t i = 0; i < prop_count; i++) {
    if (strcmp(props[i].layerName, kLayerName) == 0) {
      EXPECT_GE(kExpectedVersion, props[i].specVersion);
      layer_found = true;
      break;
    }
  }
  EXPECT_TRUE(layer_found);
}

TEST(CompactImage, DeviceExtensions) {
  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = static_cast<uint32_t>(kLayers.size()),
      .ppEnabledLayerNames = kLayers.data(),
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = nullptr,
  };
  VkInstance instance;

  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&instance_info, nullptr, &instance));

  uint32_t gpu_count;
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
  EXPECT_GE(gpu_count, 1u);

  std::vector<VkPhysicalDevice> physical_devices(gpu_count);
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices.data()));

  uint32_t prop_count;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateDeviceExtensionProperties(physical_devices[0], kLayerName,
                                                             &prop_count, nullptr));
  EXPECT_EQ(prop_count, kExpectedDeviceExtensions.size());

  std::vector<VkExtensionProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateDeviceExtensionProperties(physical_devices[0], kLayerName,
                                                             &prop_count, props.data()));
  for (uint32_t i = 0; i < prop_count; i++) {
    EXPECT_STREQ(kExpectedDeviceExtensions[i], props[i].extensionName);
  }

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = 0,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities,
  };
  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(kExpectedDeviceExtensions.size()),
      .ppEnabledExtensionNames = kExpectedDeviceExtensions.data(),
      .pEnabledFeatures = nullptr,
  };
  VkDevice device;
  EXPECT_EQ(VK_SUCCESS, vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device));
}

TEST(CompactImage, CmdWriteCompactImageMemorySizeFUCHSIA) {
  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = static_cast<uint32_t>(kLayers.size()),
      .ppEnabledLayerNames = kLayers.data(),
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = nullptr,
  };
  VkInstance instance;

  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&instance_info, nullptr, &instance));

  uint32_t gpu_count;
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
  EXPECT_GE(gpu_count, 1u);

  std::vector<VkPhysicalDevice> physical_devices(gpu_count);
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices.data()));

  VkImageFormatProperties image_format_properties;
  VkResult result = vkGetPhysicalDeviceImageFormatProperties(
      physical_devices[0], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA, &image_format_properties);
  // End test if compact images are not supported by physical device.
  if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
    return;
  }

  float queue_priorities[1] = {0.0};
  uint32_t queue_family_index = 0;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = queue_family_index,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities,
  };
  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(kExpectedDeviceExtensions.size()),
      .ppEnabledExtensionNames = kExpectedDeviceExtensions.data(),
      .pEnabledFeatures = nullptr,
  };
  VkDevice device;
  EXPECT_EQ(VK_SUCCESS, vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device));

  PFN_vkCmdWriteCompactImageMemorySizeFUCHSIA f_vkCmdWriteCompactImageMemorySizeFUCHSIA =
      reinterpret_cast<PFN_vkCmdWriteCompactImageMemorySizeFUCHSIA>(
          vkGetDeviceProcAddr(device, "vkCmdWriteCompactImageMemorySizeFUCHSIA"));
  EXPECT_TRUE(f_vkCmdWriteCompactImageMemorySizeFUCHSIA);

  uint32_t width = 600;
  uint32_t height = 1024;
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_IMAGE_CREATE_COMPACT_BIT_FUCHSIA,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = VkExtent3D{width, height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkImage image;
  EXPECT_EQ(VK_SUCCESS, vkCreateImage(device, &image_create_info, nullptr, &image));

  VkImageMemoryRequirementsInfo2 memory_requirements_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
  };
  VkMemoryDedicatedRequirements memory_dedicated_requirements = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
      .pNext = nullptr,
  };
  VkMemoryRequirements2 memory_requirements = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
      .pNext = &memory_dedicated_requirements,
  };
  vkGetImageMemoryRequirements2(device, &memory_requirements_info, &memory_requirements);
  EXPECT_TRUE(memory_dedicated_requirements.prefersDedicatedAllocation);

  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_devices[0], &memory_properties);

  uint32_t image_memory_type_index = VK_MAX_MEMORY_TYPES;
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
    if (memory_requirements.memoryRequirements.memoryTypeBits & (1 << i)) {
      image_memory_type_index = i;
      break;
    }
  }
  EXPECT_TRUE(image_memory_type_index != VK_MAX_MEMORY_TYPES);

  VkMemoryDedicatedAllocateInfo image_memory_dedicated_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = nullptr,
      .image = image,
      .buffer = VK_NULL_HANDLE,
  };
  VkMemoryAllocateInfo image_memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &image_memory_dedicated_allocate_info,
      .allocationSize = memory_requirements.memoryRequirements.size,
      .memoryTypeIndex = image_memory_type_index,
  };

  VkDeviceMemory image_memory;
  EXPECT_EQ(VK_SUCCESS, vkAllocateMemory(device, &image_memory_allocate_info, 0, &image_memory));
  EXPECT_EQ(VK_SUCCESS, vkBindImageMemory(device, image, image_memory, 0));

  // Buffer is used for both image upload and results.
  uint32_t buffer_size = width * height * 4;
  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = buffer_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
  };

  VkBuffer buffer;
  EXPECT_EQ(VK_SUCCESS, vkCreateBuffer(device, &buffer_create_info, 0, &buffer));

  uint32_t buffer_memory_type_index = VK_MAX_MEMORY_TYPES;
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
    VkMemoryPropertyFlags required_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (required_properties & memory_properties.memoryTypes[i].propertyFlags) {
      buffer_memory_type_index = i;
      break;
    }
  }
  EXPECT_TRUE(buffer_memory_type_index != VK_MAX_MEMORY_TYPES);

  VkMemoryAllocateInfo buffer_memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = nullptr,
      .allocationSize = buffer_size,
      .memoryTypeIndex = buffer_memory_type_index,
  };

  VkDeviceMemory buffer_memory;
  EXPECT_EQ(VK_SUCCESS, vkAllocateMemory(device, &buffer_memory_allocate_info, 0, &buffer_memory));
  EXPECT_EQ(VK_SUCCESS, vkBindBufferMemory(device, buffer, buffer_memory, 0));

  void* pData = nullptr;
  EXPECT_EQ(VK_SUCCESS, vkMapMemory(device, buffer_memory, 0, buffer_size, 0, &pData));

  VkCommandPoolCreateInfo command_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = queue_family_index,
  };

  VkCommandPool command_pool;
  EXPECT_EQ(VK_SUCCESS, vkCreateCommandPool(device, &command_pool_create_info, 0, &command_pool));

  VkCommandBufferAllocateInfo command_buffer_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer command_buffer;
  EXPECT_EQ(VK_SUCCESS,
            vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer));

  VkCommandBufferBeginInfo command_buffer_begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = nullptr,
  };
  EXPECT_EQ(VK_SUCCESS, vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

  VkImageSubresourceRange subresource_range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  VkImageMemoryBarrier undefined_transfer_dst_image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .image = image,
      .subresourceRange = subresource_range,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0,
                       nullptr, 1, &undefined_transfer_dst_image_memory_barrier);

  // Linear gradient.
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      reinterpret_cast<uint32_t*>(pData)[y * width + x] = 0xff0000ff | (x << 8);
    }
  }

  VkImageSubresourceLayers subresource_layers = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  VkBufferImageCopy region = {
      .bufferOffset = 0,
      .bufferRowLength = width,
      .bufferImageHeight = height,
      .imageSubresource = subresource_layers,
      .imageOffset = VkOffset3D{0, 0, 0},
      .imageExtent = VkExtent3D{width, height, 1},
  };
  vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);

  f_vkCmdWriteCompactImageMemorySizeFUCHSIA(
      command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, buffer, 0, &subresource_layers);

  VkImageMemoryBarrier transfer_dst_transfer_src_image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .image = image,
      .subresourceRange = subresource_range,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0,
                       nullptr, 1, &transfer_dst_transfer_src_image_memory_barrier);

  f_vkCmdWriteCompactImageMemorySizeFUCHSIA(
      command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 4, &subresource_layers);

  VkImageMemoryBarrier transfer_src_general_image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .image = image,
      .subresourceRange = subresource_range,
  };
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0,
                       nullptr, 1, &transfer_src_general_image_memory_barrier);

  f_vkCmdWriteCompactImageMemorySizeFUCHSIA(
      command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, buffer, 8, &subresource_layers);

  EXPECT_EQ(VK_SUCCESS, vkEndCommandBuffer(command_buffer));

  VkQueue queue;
  vkGetDeviceQueue(device, queue_family_index, 0, &queue);

  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &command_buffer,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr,
  };
  EXPECT_EQ(VK_SUCCESS, vkQueueSubmit(queue, 1, &submit_info, 0));
  EXPECT_EQ(VK_SUCCESS, vkQueueWaitIdle(queue));

  uint32_t transfer_dst_layout_size = reinterpret_cast<uint32_t*>(pData)[0];
  EXPECT_EQ(0u, transfer_dst_layout_size & 0xff000000);
  EXPECT_NE(0u, transfer_dst_layout_size);

  uint32_t transfer_src_layout_size = reinterpret_cast<uint32_t*>(pData)[1];
  EXPECT_EQ(0u, transfer_src_layout_size & 0xff000000);
  EXPECT_NE(0u, transfer_src_layout_size);

  uint32_t general_layout_size = reinterpret_cast<uint32_t*>(pData)[2];
  EXPECT_EQ(0u, general_layout_size & 0xff000000);
  EXPECT_NE(0u, general_layout_size);
}
