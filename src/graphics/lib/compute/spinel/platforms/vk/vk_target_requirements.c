// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_target_requirements.h"

#include <string.h>

#include "common/macros.h"
#include "common/vk/shader_info_amd.h"
#include "spinel_vk.h"
#include "vk_target.h"

//
// TARGET PROPERTIES: VULKAN
//
// Yields the queues, extensions and features required by a Spinel
// target.
//
// If either .qcis or .ext_names are NULL then the respective count will
// be initialized.
//
// It is an error to provide a count that is too small.
//

//
// FIXME(allanmac): IMPLEMENT SIMILAR FUNCTIONALITY IN HOTSORT
// (.shaderInt64) ONCE WE STOP USING float64/int64
//

spn_result_t
spn_vk_target_get_requirements(struct spn_vk_target const * const        target,
                               struct spn_vk_target_requirements * const requirements)
{
  //
  //
  //
  if ((target == NULL) || (requirements == NULL))
    {
      return SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS;
    }

  //
  // QUEUES
  //
  // FIXME(allanmac): we're only implementing the "SIMPLE" queueing type
  //
  assert(target->config.queueing == SPN_VK_TARGET_QUEUEING_SIMPLE);

  {
    static float const                   qp[]  = { 1.0f };
    static VkDeviceQueueCreateInfo const qis[] = { {

      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = 0,
      .queueCount       = 1,
      .pQueuePriorities = qp } };

    if (requirements->qcis == NULL)
      {
        requirements->qci_count = ARRAY_LENGTH_MACRO(qis);
      }
    else
      {
        if (requirements->qci_count < ARRAY_LENGTH_MACRO(qis))
          {
            return SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS;
          }
        else
          {
            requirements->qci_count = ARRAY_LENGTH_MACRO(qis);

            memcpy(requirements->qcis, qis, sizeof(qis));
          }
      }
  }

  //
  // REQUIRED EXTENSIONS
  //
  {
    //
    // compute number of required extensions
    //
    uint32_t ext_count = 0;

    for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(target->config.extensions.bitmap); ii++)
      {
        ext_count += __builtin_popcount(target->config.extensions.bitmap[ii]);
      }

    if (requirements->ext_names == NULL)
      {
        requirements->ext_name_count = ext_count;
      }
    else
      {
        if (requirements->ext_name_count < ext_count)
          {
            return SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS;
          }
        else
          {
            //
            // FIXME(allanmac): this can be accelerated by exploiting
            // the extension bitmap
            //
            uint32_t ii = 0;

#define SPN_VK_TARGET_EXTENSION_STRING(ext_) "VK_" STRINGIFY_MACRO(ext_)

#undef SPN_VK_TARGET_EXTENSION
#define SPN_VK_TARGET_EXTENSION(ext_)                                                              \
  if (target->config.extensions.named.ext_)                                                        \
    {                                                                                              \
      requirements->ext_names[ii++] = SPN_VK_TARGET_EXTENSION_STRING(ext_);                        \
    }

            SPN_VK_TARGET_EXTENSIONS()

            //
            // FIXME(allanmac): remove this as soon as it's verified
            // that the Intel driver does the right thing
            //
            if (target->config.extensions.named.AMD_shader_info)
              {
                vk_shader_info_amd_statistics_enable();
              }
          }
      }
  }

  //
  // REQUIRED FEATURES
  //
  {
    if (requirements->pdf != NULL)
      {
        //
        // Let's always have this on during debug
        //
#ifndef NDEBUG
        requirements->pdf->robustBufferAccess = true;
#endif
        //
        // Enable target features
        //
#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_)                                                            \
  if (target->config.features.named.feature_)                                                      \
    requirements->pdf->feature_ = true;

        SPN_VK_TARGET_FEATURES()
      }
  }

  return SPN_SUCCESS;
}

//
//
//
