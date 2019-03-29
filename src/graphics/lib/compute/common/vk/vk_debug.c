// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdio.h>
#include <stdbool.h>

#include "vk_debug.h"
#include "common/macros.h"

//
//
//

void
vk_debug_compute_props(FILE * file, VkPhysicalDeviceProperties const * const pdp)
{
  fprintf(file,
          "%-49s : %u\n",
          "maxComputeSharedMemorySize",
          pdp->limits.maxComputeSharedMemorySize);

  fprintf(file,
          "%-49s : ( %u, %u, %u )\n",
          "maxComputeWorkGroupCount",
          pdp->limits.maxComputeWorkGroupCount[0],
          pdp->limits.maxComputeWorkGroupCount[1],
          pdp->limits.maxComputeWorkGroupCount[2]);

  fprintf(file,
          "%-49s : %u\n",
          "maxComputeWorkGroupInvocations",
          pdp->limits.maxComputeWorkGroupInvocations);

  fprintf(file,
          "%-49s : ( %u, %u, %u )\n\n",
          "maxComputeWorkGroupSize",
          pdp->limits.maxComputeWorkGroupSize[0],
          pdp->limits.maxComputeWorkGroupSize[1],
          pdp->limits.maxComputeWorkGroupSize[2]);
}

//
//
//

void
vk_debug_subgroup_props(FILE * file, VkPhysicalDeviceSubgroupProperties const * const pdsp)
{
  fprintf(file,
          "%-49s : %u\n",
          "subgroupSize",
          pdsp->subgroupSize);

  fprintf(file,
          "%-49s : %s\n",
          "quadOperationsInAllStages",
          pdsp->quadOperationsInAllStages ? "true" : "false");

#define DEBUG_PDSP_SHADER_STAGE(pdsp,bit)                       \
  fprintf(file,                                                 \
          "    %-45s : %s\n",                                   \
          STRINGIFY_MACRO(bit),                                 \
          (pdsp->supportedStages & bit) ? "true" : "false")

  fprintf(file,"supportedStages\n");
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_VERTEX_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_GEOMETRY_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_FRAGMENT_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_COMPUTE_BIT);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_ALL_GRAPHICS);
#if 0
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_RAYGEN_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_ANY_HIT_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_MISS_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_INTERSECTION_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_CALLABLE_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_TASK_BIT_NV);
  DEBUG_PDSP_SHADER_STAGE(pdsp,VK_SHADER_STAGE_MESH_BIT_NV);
#endif

#define DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,op)                    \
  fprintf(file,                                                 \
          "    %-45s : %s\n",                                   \
          STRINGIFY_MACRO(op),                                  \
          (pdsp->supportedOperations & op) ? "true" : "false")

  fprintf(file,"supportedOperations\n");
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_BASIC_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_VOTE_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_BALLOT_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_SHUFFLE_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_CLUSTERED_BIT);
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_QUAD_BIT);
#if 0
  DEBUG_PDSP_SUBGROUP_FEATURE(pdsp,VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV);
#endif
  fprintf(file,"\n");
}

//
//
//

VkBool32
VKAPI_PTR
vk_debug_report_cb(VkDebugReportFlagsEXT      flags,
                   VkDebugReportObjectTypeEXT objectType,
                   uint64_t                   object,
                   size_t                     location,
                   int32_t                    messageCode,
                   char const *               pLayerPrefix,
                   char const *               pMessage,
                   void       *               pUserData)
{
  char const * flag_str = "";
  bool         is_error = false;

#define VK_FLAG_CASE_TO_STRING(c)               \
  case c:                                       \
    flag_str = #c;                              \
    is_error = true;                            \
    break

  switch (flags)
    {
      // VK_FLAG_CASE_TO_STRING(VK_DEBUG_REPORT_INFORMATION_BIT_EXT);
      VK_FLAG_CASE_TO_STRING(VK_DEBUG_REPORT_WARNING_BIT_EXT);
      VK_FLAG_CASE_TO_STRING(VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT);
      VK_FLAG_CASE_TO_STRING(VK_DEBUG_REPORT_ERROR_BIT_EXT);
      VK_FLAG_CASE_TO_STRING(VK_DEBUG_REPORT_DEBUG_BIT_EXT);
    }

  if (is_error)
    {
      fprintf(stderr,
              "%s  %s  %s\n",
              flag_str,
              pLayerPrefix,
              pMessage);
    }

  return VK_FALSE;
}

//
//
//
