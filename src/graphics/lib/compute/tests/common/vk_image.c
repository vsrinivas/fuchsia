// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_image.h"

#include <stdbool.h>

#include "tests/common/utils.h"     // For ASSERT() macros.
#include "tests/common/vk_utils.h"  // For vk() macro.

void
vk_image_alloc_generic(vk_image_t *                  image,
                       VkFormat                      image_format,
                       VkExtent2D                    image_extent,
                       VkImageTiling                 image_tiling,
                       VkImageUsageFlags             image_usage,
                       VkImageLayout                 image_layout,
                       VkMemoryPropertyFlags         memory_flags,
                       uint32_t                      queue_families_count,
                       const uint32_t *              queue_families,
                       VkPhysicalDevice              physical_device,
                       VkDevice                      device,
                       const VkAllocationCallbacks * allocator)
{
  *image = (const vk_image_t){};

  const VkImageCreateInfo createInfo = {
    .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format    = image_format,
    .extent    = {
        .width  = image_extent.width,
        .height = image_extent.height,
        .depth  = 1,
    },
    .mipLevels             = 1,
    .arrayLayers           = 1,
    .samples               = VK_SAMPLE_COUNT_1_BIT,
    .tiling                = image_tiling,
    .usage                 = image_usage,
    .sharingMode           = (queue_families_count ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE),
    .queueFamilyIndexCount = queue_families_count,
    .pQueueFamilyIndices   = (queue_families_count ? queue_families : NULL),
    .initialLayout         = image_layout,
  };

  // Sanity checks for |image_usage| and |image_format|. If the values are not
  // compatible for this device, CreateImage() will work (though the validation
  // layer will complain about it), but rendering to the image may later fail in
  // totally unexpected ways depending on the GPU driver.
  VkFormatProperties format_props;
  vkGetPhysicalDeviceFormatProperties(physical_device, image_format, &format_props);
  switch (image_tiling)
    {
      case VK_IMAGE_TILING_OPTIMAL:
        ASSERT_MSG(
          vk_check_image_usage_vs_format_features(image_tiling, format_props.optimalTilingFeatures),
          "Creating image with VK_IMAGE_TILING_OPTIMAL is not supported by format %d\n",
          image_format);
        break;
      case VK_IMAGE_TILING_LINEAR:
        ASSERT_MSG(
          vk_check_image_usage_vs_format_features(image_tiling, format_props.linearTilingFeatures),
          "Create image with VK_IMAGE_TILING_LINEAR is not supported by format %d\n",
          image_format);
      default:
        ASSERT_MSG(false, "Unsupported VkImageTiling value %d\n", image_tiling);
    }

  vk(CreateImage(device, &createInfo, allocator, &image->image));

  // Get its memory requirements to ensure we have the right memory type.
  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(device, image->image, &memory_requirements);
  image->size                = memory_requirements.size;
  image->extent              = image_extent;
  image->memory_requirements = memory_requirements;
  image->tiling              = image_tiling;

  // Find the right memory type for this image. We want it to be host-visible.
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

  uint32_t memory_type_index = UINT32_MAX;
  {
    for (uint32_t n = 0; n < memory_properties.memoryTypeCount; n++)
      {
        if ((memory_requirements.memoryTypeBits & (1u << n)) == 0)
          continue;

        if ((memory_properties.memoryTypes[n].propertyFlags & memory_flags) == memory_flags)
          {
            memory_type_index = n;
            break;
          }
      }
    ASSERT_MSG(memory_type_index != UINT32_MAX, "Could not find memory type for image!\n");
    image->memory_type_index = memory_type_index;
  }

  // Allocate memory for our buffer. No need for a custom allocator in our
  // trivial application.
  const VkMemoryAllocateInfo allocateInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = memory_requirements.size,
    .memoryTypeIndex = memory_type_index,
  };

  vk(AllocateMemory(device, &allocateInfo, allocator, &image->memory));

  // Bind the memory to the buffer.
  vk(BindImageMemory(device, image->image, image->memory, 0));

  // Create image view.
  const VkImageViewCreateInfo viewCreateInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = image->image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = image_format,
    .components = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange = {
      .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel   = 0,
      .levelCount     = 1,
      .baseArrayLayer = 0,
      .layerCount     = 1,
    },
  };
  vk(CreateImageView(device, &viewCreateInfo, allocator, &image->image_view));

  image->device    = device;
  image->allocator = allocator;
}

void
vk_image_alloc_device_local(vk_image_t *                  image,
                            VkFormat                      image_format,
                            VkExtent2D                    image_extent,
                            VkImageUsageFlags             image_usage,
                            VkPhysicalDevice              physical_device,
                            VkDevice                      device,
                            const VkAllocationCallbacks * allocator)
{
  VkFormatProperties format_props;
  vkGetPhysicalDeviceFormatProperties(physical_device, image_format, &format_props);

  VkImageTiling image_tiling;
  // Use VK_IMAGE_TILING_OPTIONAL unless the device does not support |image_usage|
  if (vk_check_image_usage_vs_format_features(image_usage, format_props.optimalTilingFeatures))
    image_tiling = VK_IMAGE_TILING_OPTIMAL;
  else if (vk_check_image_usage_vs_format_features(image_usage, format_props.linearTilingFeatures))
    image_tiling = VK_IMAGE_TILING_LINEAR;
  else
    ASSERT_MSG(false,
               "Device does not support image usage %X for format %d\n",
               image_usage,
               image_format);

  vk_image_alloc_generic(image,
                         image_format,
                         image_extent,
                         image_tiling,
                         image_usage,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0,
                         NULL,
                         physical_device,
                         device,
                         allocator);
}

void
vk_image_free(vk_image_t * image)
{
  if (image->image != VK_NULL_HANDLE)
    {
      vkDestroyImageView(image->device, image->image_view, image->allocator);
      vkDestroyImage(image->device, image->image, image->allocator);
      vkFreeMemory(image->device, image->memory, image->allocator);
      image->image_view = VK_NULL_HANDLE;
      image->image      = VK_NULL_HANDLE;
      image->memory     = VK_NULL_HANDLE;
      image->device     = VK_NULL_HANDLE;
      image->allocator  = NULL;
    }
}
