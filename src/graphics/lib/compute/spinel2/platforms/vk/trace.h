// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TRACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TRACE_H_

//
// PLATFORM?
//

#ifdef __Fuchsia__
#include <lib/trace/event.h>
#endif

#ifdef SPN_VK_ENABLE_DEBUG_UTILS
#include "common/vk/debug_utils.h"
#endif

//
// Invoked tracing functions pass the name of the calling function and the line
// number of the invocation.
//
// clang-format off
#if !defined(SPN_VK_DISABLE_TRACE) && (defined(SPN_VK_ENABLE_DEBUG_UTILS) || (defined(__Fuchsia__) && !defined(NTRACE)))

#define SPN_VK_TRACE_DEFINE(func_, ...) func_##_trace(__VA_ARGS__, char const * trace_name_, uint32_t trace_line_)
#define SPN_VK_TRACE_INVOKE(func_, ...) func_##_trace(__VA_ARGS__, __func__, __LINE__)

#else

#define SPN_VK_TRACE_DEFINE(func_, ...) func_(__VA_ARGS__)

#endif
// clang-format on

//
// HOST
//

#if !defined(SPN_VK_DISABLE_TRACE) && (defined(__Fuchsia__) && !defined(NTRACE))

#define SPN_VK_TRACE_CATEGORY "gfx"

#define SPN_VK_TRACE_HOST_DURATION_BEGIN()                                                         \
  TRACE_DURATION_BEGIN(SPN_VK_TRACE_CATEGORY, /**/                                                 \
                       trace_name_,                                                                \
                       "line",                                                                     \
                       TA_UINT32(trace_line_))

#define SPN_VK_TRACE_HOST_DURATION_END()                                                           \
  TRACE_DURATION_END(SPN_VK_TRACE_CATEGORY, /**/                                                   \
                     trace_name_)

#define SPN_VK_TRACE_HOST_DURATION_BEGIN_REGION(region_name_)                                      \
  TRACE_DURATION_BEGIN(SPN_VK_TRACE_CATEGORY, /**/                                                 \
                       trace_name_,                                                                \
                       "line",                                                                     \
                       TA_UINT32(trace_line_),                                                     \
                       "region",                                                                   \
                       TA_STRING(region_name_))

#define SPN_VK_TRACE_HOST_DURATION_END_REGION()                                                    \
  TRACE_DURATION_END(SPN_VK_TRACE_CATEGORY, /**/                                                   \
                     trace_name_)

#else

#define SPN_VK_TRACE_HOST_DURATION_BEGIN()
#define SPN_VK_TRACE_HOST_DURATION_END()

#define SPN_VK_TRACE_HOST_DURATION_BEGIN_REGION(region_name_)
#define SPN_VK_TRACE_HOST_DURATION_END_REGION()

#endif

//
// DEVICE
//

#if !defined(SPN_VK_DISABLE_TRACE) && defined(SPN_VK_ENABLE_DEBUG_UTILS)

#define SPN_VK_TRACE_DEVICE_BEGIN_COMMAND_BUFFER(cb_)                                              \
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)                                                    \
    {                                                                                              \
      VkDebugUtilsLabelEXT const label = {                                                         \
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,                                     \
        .pNext      = NULL,                                                                        \
        .pLabelName = trace_name_,                                                                 \
        .color      = { 0.0f, 0.0f, 0.0f, 0.0f },                                                  \
      };                                                                                           \
                                                                                                   \
      pfn_vkCmdBeginDebugUtilsLabelEXT(cb_, &label);                                               \
    }

#define SPN_VK_TRACE_DEVICE_END_COMMAND_BUFFER(cb_)                                                \
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)                                                      \
    {                                                                                              \
      pfn_vkCmdEndDebugUtilsLabelEXT(cb_);                                                         \
    }

#else

#define SPN_VK_TRACE_DEVICE_BEGIN_COMMAND_BUFFER(cb_)
#define SPN_VK_TRACE_DEVICE_END_COMMAND_BUFFER(cb_)

#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TRACE_H_
