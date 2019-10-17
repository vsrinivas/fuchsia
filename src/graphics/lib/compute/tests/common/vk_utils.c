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
