// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "shader_info_amd.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

//
//
//

void
vk_shader_info_amd_statistics(VkDevice           device,
                              VkPipeline         p[],
                              char const * const names[],
                              uint32_t const     count)
{
  PFN_vkGetShaderInfoAMD vkGetShaderInfoAMD =
    (PFN_vkGetShaderInfoAMD)vkGetDeviceProcAddr(device, "vkGetShaderInfoAMD");

  if (vkGetShaderInfoAMD == NULL)
    return;

  fprintf(
    stdout,
    "                                   PHY   PHY  AVAIL AVAIL\n"
    "VGPRs SGPRs LDS_MAX LDS/WG  SPILL VGPRs SGPRs VGPRs SGPRs  WORKGROUP_SIZE              NAME\n");

  for (uint32_t ii = 0; ii < count; ii++)
    {
      VkShaderStatisticsInfoAMD ssi_amd;
      size_t                    ssi_amd_size = sizeof(ssi_amd);

      if (vkGetShaderInfoAMD(device,
                             p[ii],
                             VK_SHADER_STAGE_COMPUTE_BIT,
                             VK_SHADER_INFO_TYPE_STATISTICS_AMD,
                             &ssi_amd_size,
                             &ssi_amd) == VK_SUCCESS)
        {
          fprintf(stdout,
                  "%5" PRIu32 " "
                  "%5" PRIu32 "   "
                  "%5" PRIu32 " "

                  "%6zu "
                  "%6zu "

                  "%5" PRIu32 " "
                  "%5" PRIu32 " "
                  "%5" PRIu32 " "
                  "%5" PRIu32 "  "

                  "( %6" PRIu32 ", "
                  "%6" PRIu32 ", "
                  "%6" PRIu32 " )  ",
                  ssi_amd.resourceUsage.numUsedVgprs,
                  ssi_amd.resourceUsage.numUsedSgprs,
                  ssi_amd.resourceUsage.ldsSizePerLocalWorkGroup,
                  ssi_amd.resourceUsage.ldsUsageSizeInBytes,     // size_t
                  ssi_amd.resourceUsage.scratchMemUsageInBytes,  // size_t
                  ssi_amd.numPhysicalVgprs,
                  ssi_amd.numPhysicalSgprs,
                  ssi_amd.numAvailableVgprs,
                  ssi_amd.numAvailableSgprs,
                  ssi_amd.computeWorkGroupSize[0],
                  ssi_amd.computeWorkGroupSize[1],
                  ssi_amd.computeWorkGroupSize[2]);

          if (names != NULL)
            fprintf(stdout, "%s\n", names[ii]);
          else
            fprintf(stdout, "---\n");
        }
    }
}

//
//
//

void
vk_shader_info_amd_disassembly(VkDevice           device,
                               VkPipeline         p[],
                               char const * const names[],
                               uint32_t const     count)
{
  PFN_vkGetShaderInfoAMD vkGetShaderInfoAMD =
    (PFN_vkGetShaderInfoAMD)vkGetDeviceProcAddr(device, "vkGetShaderInfoAMD");

  if (vkGetShaderInfoAMD == NULL)
    return;

  for (uint32_t ii = 0; ii < count; ii++)
    {
      size_t disassembly_amd_size;

      if (vkGetShaderInfoAMD(device,
                             p[ii],
                             VK_SHADER_STAGE_COMPUTE_BIT,
                             VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD,
                             &disassembly_amd_size,
                             NULL) == VK_SUCCESS)
        {
          void * disassembly_amd = malloc(disassembly_amd_size);

          if (vkGetShaderInfoAMD(device,
                                 p[ii],
                                 VK_SHADER_STAGE_COMPUTE_BIT,
                                 VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD,
                                 &disassembly_amd_size,
                                 disassembly_amd) == VK_SUCCESS)
            {
              if (names != NULL)
                fprintf(stdout, "SHADER: %s\n", names[ii]);

              fprintf(stdout, "%s", (char *)disassembly_amd);
            }

          free(disassembly_amd);
        }
    }
}

//
//
//
