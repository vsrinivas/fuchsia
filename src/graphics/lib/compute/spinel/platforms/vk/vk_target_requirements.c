// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_target_requirements.h"

#include <string.h>

#include "common/macros.h"
#include "spinel_vk.h"
#include "vk_target.h"

//
// TARGET PROPERTIES: VULKAN
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
  //
  //
  bool is_success = true;

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

        is_success = false;
      }
    else
      {
        if (requirements->qci_count < ARRAY_LENGTH_MACRO(qis))
          {
            is_success = false;
          }
        else
          {
            requirements->qci_count = ARRAY_LENGTH_MACRO(qis);

            memcpy(requirements->qcis, qis, sizeof(qis));
          }
      }
  }

  //
  // EXTENSIONS
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

        is_success = false;
      }
    else
      {
        if (requirements->ext_name_count < ext_count)
          {
            is_success = false;
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
          }
      }
  }

  //
  // FEATURES
  //
  if (requirements->pdf2 == NULL)
    {
      is_success = false;
    }
  else
    {
      //
      // Let's always have this on during debug
      //
#ifndef NDEBUG
      requirements->pdf2->features.robustBufferAccess = true;
#endif
      //
      // Enable target features
      //
#undef SPN_VK_TARGET_FEATURE
#define SPN_VK_TARGET_FEATURE(feature_)                                                            \
  if (target->config.features.named.feature_)                                                      \
    requirements->pdf2->features.feature_ = true;

      SPN_VK_TARGET_FEATURES()
    }

  //
  // FEATURES2
  //
  // Ensure that *all* of the required feature flags are enabled
  //
  union spn_vk_target_feature_structures structures = target->config.structures;

  VkPhysicalDeviceFeatures2 * const pdf2 = requirements->pdf2;

  if (pdf2 == NULL)
    {
      is_success = false;
    }
  else
    {
      VkBaseOutStructure * bos = pdf2->pNext;

      while (bos != NULL)
        {
#undef SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X
#define SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBER_X(feature_, bX_)                                    \
  ((VkPhysicalDevice##feature_ *)bos)->bX_ |= structures.named.feature_.bX_;                       \
  structures.named.feature_.bX_ = 0;

#undef SPN_VK_TARGET_FEATURE_STRUCTURE
#define SPN_VK_TARGET_FEATURE_STRUCTURE(feature_, stype_, ...)                                     \
  case stype_:                                                                                     \
    SPN_VK_TARGET_FEATURE_STRUCTURE_MEMBERS(feature_, __VA_ARGS__)                                 \
    break;

          switch (bos->sType)
            {
              SPN_VK_TARGET_FEATURE_STRUCTURES()

              default:
                break;
            }

          bos = bos->pNext;
        }

      //
      // It's an error if any bit is still lit -- we can't reliably execute
      // the Spinel target unless the VkDevice is intialized with all
      // required feature structure members.
      //
      for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(structures.bitmap); ii++)
        {
          if (structures.bitmap[ii] != 0)
            {
              is_success = false;
              break;
            }
        }
    }

  //
  //
  //
  return is_success ? SPN_SUCCESS : SPN_ERROR_PARTIAL_TARGET_REQUIREMENTS;
}

//
//
//
