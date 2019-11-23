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
