// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// clang-format off
#define VK_LIST_RESULT_VALUES(macro)        \
  macro(SUCCESS)                            \
  macro(NOT_READY)                          \
  macro(EVENT_SET)                          \
  macro(INCOMPLETE)                         \
  macro(ERROR_OUT_OF_HOST_MEMORY)           \
  macro(ERROR_OUT_OF_DEVICE_MEMORY)         \
  macro(ERROR_INITIALIZATION_FAILED)        \
  macro(ERROR_DEVICE_LOST)                  \
  macro(ERROR_MEMORY_MAP_FAILED)            \
  macro(ERROR_LAYER_NOT_PRESENT)            \
  macro(ERROR_EXTENSION_NOT_PRESENT)        \
  macro(ERROR_FEATURE_NOT_PRESENT)          \
  macro(ERROR_INCOMPATIBLE_DRIVER)          \
  macro(ERROR_TOO_MANY_OBJECTS)             \
  macro(ERROR_FORMAT_NOT_SUPPORTED)         \
  macro(ERROR_FRAGMENTED_POOL)              \
  macro(ERROR_OUT_OF_POOL_MEMORY)           \
  macro(ERROR_INVALID_EXTERNAL_HANDLE)      \
  macro(ERROR_SURFACE_LOST_KHR)             \
  macro(ERROR_NATIVE_WINDOW_IN_USE_KHR)     \
  macro(SUBOPTIMAL_KHR)                     \
  macro(ERROR_OUT_OF_DATE_KHR)              \
  macro(ERROR_INCOMPATIBLE_DISPLAY_KHR)     \
  macro(ERROR_VALIDATION_FAILED_EXT)        \
  macro(ERROR_INVALID_SHADER_NV)            \
  macro(ERROR_FRAGMENTATION_EXT)            \
  macro(ERROR_NOT_PERMITTED_EXT)

// clang-format on

// Note: for simplicity, this returns the address of a mutable static buffer
// if the value is unknown. Not thread-safe, but should not matter in practice.
// Allows to detect unexpected or exotic result values, and the list above
// can be updated if the spec adds new ones.
const char *
vk_result_to_string(VkResult result)
{
  switch (result)
    {
#define HANDLE_VALUE(result)                                                                       \
  case VK_##result:                                                                                \
    return "VK_" #result;

      VK_LIST_RESULT_VALUES(HANDLE_VALUE)

#undef HANDLE_VALUE

        default: {
          static char temp[16] = {};
          snprintf(temp, sizeof(temp), "VkResult(%u)", (uint32_t)result);
          return temp;
        }
    }
}

void
vk_panic_(VkResult result, const char * file, int line, const char * fmt, ...)
{
  fprintf(stderr, "%s:%d:PANIC(%s):", file, line, vk_result_to_string(result));
  if (fmt)
    {
      va_list args;
      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
    }
  fprintf(stderr, "\n");
  abort();
}

void
vk_instance_create_info_print(const VkInstanceCreateInfo * info)
{
  fprintf(stderr, "Instance create info:\n");
  fprintf(stderr, "  flags:          %u\n", info->flags);

  const VkApplicationInfo * appInfo = info->pApplicationInfo;
  fprintf(stderr, "  app info:\n");
  fprintf(stderr, "    app name:       %s\n", appInfo->pApplicationName);
  fprintf(stderr, "    app version:    %u\n", appInfo->applicationVersion);
  fprintf(stderr, "    engine name:    %s\n", appInfo->pEngineName);
  fprintf(stderr, "    engine version: %u\n", appInfo->engineVersion);
  fprintf(stderr, "    api version:    %u\n", appInfo->apiVersion);

  fprintf(stderr, "  layers (%u): ", info->enabledLayerCount);
  for (uint32_t nn = 0; nn < info->enabledLayerCount; ++nn)
    fprintf(stderr, " %s", info->ppEnabledLayerNames[nn]);
  fprintf(stderr, "\n  extensions (%u): ", info->enabledExtensionCount);
  for (uint32_t nn = 0; nn < info->enabledExtensionCount; ++nn)
    fprintf(stderr, " %s", info->ppEnabledExtensionNames[nn]);
  fprintf(stderr, "\n");
}

void
vk_device_create_info_print(const VkDeviceCreateInfo * info)
{
  fprintf(stderr, "Device creation info:\n");
  fprintf(stderr, "  flags:                 %u\n", info->flags);
  fprintf(stderr, "  queueCreateInfoCount:  %u\n", info->queueCreateInfoCount);
  if (info->queueCreateInfoCount)
    {
      for (uint32_t nn = 0; nn < info->queueCreateInfoCount; ++nn)
        {
          const VkDeviceQueueCreateInfo * qinfo = info->pQueueCreateInfos + nn;
          fprintf(stderr, "    [%d] flags:       %u\n", nn, qinfo->flags);
          fprintf(stderr, "        familyIndex: %u\n", qinfo->queueFamilyIndex);
          fprintf(stderr, "        count:       %u\n", qinfo->queueCount);
          if (qinfo->queueCount > 0)
            {
              fprintf(stderr, "        priorities: ");
              for (uint32_t mm = 0; mm < qinfo->queueCount; ++mm)
                fprintf(stderr, " %f", qinfo->pQueuePriorities[mm]);
              fprintf(stderr, "\n");
            }
        }
    }
  fprintf(stderr, "  extensions (%d): ", info->enabledExtensionCount);
  for (uint32_t nn = 0; nn < info->enabledExtensionCount; ++nn)
    fprintf(stderr, " %s", info->ppEnabledExtensionNames[nn]);
  if (info->pEnabledFeatures)
    {
      fprintf(stderr, "\n  features:\n");

#define CHECK_FEATURE(feature)                                                                     \
  do                                                                                               \
    {                                                                                              \
      if (info->pEnabledFeatures->feature == VK_TRUE)                                              \
        {                                                                                          \
          fprintf(stderr, "    %s\n", #feature);                                                   \
        }                                                                                          \
    }                                                                                              \
  while (0);

      CHECK_FEATURE(robustBufferAccess)
      CHECK_FEATURE(shaderInt64)
      CHECK_FEATURE(shaderFloat64)

#undef CHECK_FEATURE

      // Memory dump.
      {
        const uint32_t * data      = (const uint32_t *)info->pEnabledFeatures;
        size_t           data_size = sizeof(info->pEnabledFeatures[0]);
        for (uint32_t nn = 0; nn < data_size / 4; ++nn)
          {
            fprintf(stderr, " %02X", data[nn]);
            if ((nn + 1) % 16 == 0)
              fprintf(stderr, "\n");
          }
        fprintf(stderr, "\n");
      }
    }
}

bool
vk_check_image_usage_vs_format_features(VkImageUsageFlags    image_usage,
                                        VkFormatFeatureFlags format_features)
{
// Helper macro. |usage_| and |feature_| are abbreviated image usage and format
// feature bit constants, respectively.
#define CHECK_COMBO(usage_, feature_)                                                              \
  if ((image_usage & VK_IMAGE_USAGE_##usage_##_BIT) != 0 &&                                        \
      (format_features & VK_FORMAT_FEATURE_##feature_##_BIT) == 0)                                 \
    {                                                                                              \
      return false;                                                                                \
    }

  CHECK_COMBO(TRANSFER_SRC, TRANSFER_SRC)
  CHECK_COMBO(TRANSFER_DST, TRANSFER_DST)
  CHECK_COMBO(SAMPLED, SAMPLED_IMAGE)
  CHECK_COMBO(STORAGE, STORAGE_IMAGE)
  CHECK_COMBO(COLOR_ATTACHMENT, COLOR_ATTACHMENT)
  CHECK_COMBO(DEPTH_STENCIL_ATTACHMENT, DEPTH_STENCIL_ATTACHMENT)

  // CHECK_COMBO(TRANSIENT_ATTACHMENT, )  // No matching format feature flag.
  // CHECK_COMBO(INPUT_ATTACHMENT, )      // No matching format feature flag.

#undef CHECK_COMBO
  return true;
}

void
vk_submit_one(VkSemaphore          wait_semaphore,
              VkPipelineStageFlags wait_stages,
              VkSemaphore          signal_semaphore,
              VkQueue              command_queue,
              VkCommandBuffer      command_buffer,
              VkFence              signal_fence)
{
  VkSubmitInfo submit_info = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = (wait_semaphore != VK_NULL_HANDLE) ? 1 : 0,
    .pWaitSemaphores      = (wait_semaphore != VK_NULL_HANDLE) ? &wait_semaphore : NULL,
    .pWaitDstStageMask    = (wait_semaphore != VK_NULL_HANDLE) ? &wait_stages : NULL,
    .commandBufferCount   = (command_buffer != VK_NULL_HANDLE) ? 1 : 0,
    .pCommandBuffers      = (command_buffer != VK_NULL_HANDLE) ? &command_buffer : NULL,
    .signalSemaphoreCount = (signal_semaphore != VK_NULL_HANDLE) ? 1 : 0,
    .pSignalSemaphores    = (signal_semaphore != VK_NULL_HANDLE) ? &signal_semaphore : NULL,
  };
  vk(QueueSubmit(command_queue, 1, &submit_info, signal_fence));
}

uint32_t
vk_format_to_bytes_per_pixel(VkFormat format)
{
  switch (format)
    {
      case VK_FORMAT_R4G4_UNORM_PACK8:
      case VK_FORMAT_R8_UNORM:
      case VK_FORMAT_R8_SNORM:
      case VK_FORMAT_R8_USCALED:
      case VK_FORMAT_R8_SSCALED:
      case VK_FORMAT_R8_UINT:
      case VK_FORMAT_R8_SINT:
      case VK_FORMAT_R8_SRGB:
        return 1;

      case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
      case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
      case VK_FORMAT_R5G6B5_UNORM_PACK16:
      case VK_FORMAT_B5G6R5_UNORM_PACK16:
      case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
      case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
      case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
      case VK_FORMAT_R8G8_UNORM:
      case VK_FORMAT_R8G8_SNORM:
      case VK_FORMAT_R8G8_USCALED:
      case VK_FORMAT_R8G8_SSCALED:
      case VK_FORMAT_R8G8_UINT:
      case VK_FORMAT_R8G8_SINT:
      case VK_FORMAT_R8G8_SRGB:
      case VK_FORMAT_R16_UNORM:
      case VK_FORMAT_R16_SNORM:
      case VK_FORMAT_R16_USCALED:
      case VK_FORMAT_R16_SSCALED:
      case VK_FORMAT_R16_UINT:
      case VK_FORMAT_R16_SINT:
      case VK_FORMAT_R16_SFLOAT:
        return 2;

      case VK_FORMAT_R8G8B8_UNORM:
      case VK_FORMAT_R8G8B8_SNORM:
      case VK_FORMAT_R8G8B8_USCALED:
      case VK_FORMAT_R8G8B8_SSCALED:
      case VK_FORMAT_R8G8B8_UINT:
      case VK_FORMAT_R8G8B8_SINT:
      case VK_FORMAT_R8G8B8_SRGB:
      case VK_FORMAT_B8G8R8_UNORM:
      case VK_FORMAT_B8G8R8_SNORM:
      case VK_FORMAT_B8G8R8_USCALED:
      case VK_FORMAT_B8G8R8_SSCALED:
      case VK_FORMAT_B8G8R8_UINT:
      case VK_FORMAT_B8G8R8_SINT:
      case VK_FORMAT_B8G8R8_SRGB:
        return 3;

      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SNORM:
      case VK_FORMAT_R8G8B8A8_USCALED:
      case VK_FORMAT_R8G8B8A8_SSCALED:
      case VK_FORMAT_R8G8B8A8_UINT:
      case VK_FORMAT_R8G8B8A8_SINT:
      case VK_FORMAT_R8G8B8A8_SRGB:
      case VK_FORMAT_B8G8R8A8_UNORM:
      case VK_FORMAT_B8G8R8A8_SNORM:
      case VK_FORMAT_B8G8R8A8_USCALED:
      case VK_FORMAT_B8G8R8A8_SSCALED:
      case VK_FORMAT_B8G8R8A8_UINT:
      case VK_FORMAT_B8G8R8A8_SINT:
      case VK_FORMAT_B8G8R8A8_SRGB:
      case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
      case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
      case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
      case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
      case VK_FORMAT_A8B8G8R8_UINT_PACK32:
      case VK_FORMAT_A8B8G8R8_SINT_PACK32:
      case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
      case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
      case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
      case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
      case VK_FORMAT_A2R10G10B10_UINT_PACK32:
      case VK_FORMAT_A2R10G10B10_SINT_PACK32:
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
      case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
      case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
      case VK_FORMAT_A2B10G10R10_UINT_PACK32:
      case VK_FORMAT_A2B10G10R10_SINT_PACK32:
      case VK_FORMAT_R16G16_UNORM:
      case VK_FORMAT_R16G16_SNORM:
      case VK_FORMAT_R16G16_USCALED:
      case VK_FORMAT_R16G16_SSCALED:
      case VK_FORMAT_R16G16_UINT:
      case VK_FORMAT_R16G16_SINT:
      case VK_FORMAT_R16G16_SFLOAT:
      case VK_FORMAT_R32_UINT:
      case VK_FORMAT_R32_SINT:
      case VK_FORMAT_R32_SFLOAT:
      case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return 4;

      case VK_FORMAT_R16G16B16_UNORM:
      case VK_FORMAT_R16G16B16_SNORM:
      case VK_FORMAT_R16G16B16_USCALED:
      case VK_FORMAT_R16G16B16_SSCALED:
      case VK_FORMAT_R16G16B16_UINT:
      case VK_FORMAT_R16G16B16_SINT:
      case VK_FORMAT_R16G16B16_SFLOAT:
      case VK_FORMAT_R32G32B32_UINT:
      case VK_FORMAT_R32G32B32_SINT:
      case VK_FORMAT_R32G32B32_SFLOAT:
        return 6;

      case VK_FORMAT_R16G16B16A16_UNORM:
      case VK_FORMAT_R16G16B16A16_SNORM:
      case VK_FORMAT_R16G16B16A16_USCALED:
      case VK_FORMAT_R16G16B16A16_SSCALED:
      case VK_FORMAT_R16G16B16A16_UINT:
      case VK_FORMAT_R16G16B16A16_SINT:
      case VK_FORMAT_R16G16B16A16_SFLOAT:
      case VK_FORMAT_R32G32_UINT:
      case VK_FORMAT_R32G32_SINT:
      case VK_FORMAT_R32G32_SFLOAT:
      case VK_FORMAT_R64_UINT:
      case VK_FORMAT_R64_SINT:
      case VK_FORMAT_R64_SFLOAT:
        return 8;

      case VK_FORMAT_R32G32B32A32_UINT:
      case VK_FORMAT_R32G32B32A32_SINT:
      case VK_FORMAT_R32G32B32A32_SFLOAT:
      case VK_FORMAT_R64G64_UINT:
      case VK_FORMAT_R64G64_SINT:
      case VK_FORMAT_R64G64_SFLOAT:
        return 16;

      case VK_FORMAT_R64G64B64_UINT:
      case VK_FORMAT_R64G64B64_SINT:
      case VK_FORMAT_R64G64B64_SFLOAT:
        return 24;

      case VK_FORMAT_R64G64B64A64_UINT:
      case VK_FORMAT_R64G64B64A64_SINT:
      case VK_FORMAT_R64G64B64A64_SFLOAT:
        return 32;

      default:
        // All other formats correspond to stencil/depth or compressed formats.
        return 0;
    }
}
