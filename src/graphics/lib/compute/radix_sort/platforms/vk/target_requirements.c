// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "target_requirements.h"

#include <stdio.h>

#include "common/macros.h"
#include "radix_sort/platforms/vk/radix_sort_vk.h"
#include "target.h"
#include "target_archive/target_archive.h"

//
//
//

struct radix_sort_vk_target
{
  struct target_archive_header ar_header;
};

//
// RADIX_SORT TARGET REQUIREMENTS: VULKAN
//

bool
radix_sort_vk_target_get_requirements(struct radix_sort_vk_target const *        target,
                                      struct radix_sort_vk_target_requirements * requirements)
{
  //
  // Must not be NULL
  //
  if ((target == NULL) || (requirements == NULL))
    {
      return false;
    }

#ifndef RS_VK_DISABLE_VERIFY
  //
  // Verify target archive is valid archive
  //
  if (target->ar_header.magic != TARGET_ARCHIVE_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Invalid target -- missing magic.");
#endif
      return NULL;
    }
#endif

  //
  // Get the target header.
  //
  struct target_archive_header const * const ar_header  = &target->ar_header;
  struct target_archive_entry const * const  ar_entries = ar_header->entries;
  uint32_t const * const                     ar_data    = ar_entries[ar_header->count - 1].data;

  union
  {
    struct radix_sort_vk_target_header const * header;
    uint32_t const *                           data;
  } const rs_header_data = { .data = ar_data };

#ifndef RS_VK_DISABLE_VERIFY
  //
  // Verify target is compatible with the library.
  //
  if (rs_header_data.header->magic != RS_HEADER_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Target is not compatible with library.");
#endif
      return NULL;
    }
#endif

  //
  //
  //
  bool is_ok = true;

  //
  // EXTENSIONS
  //
  {
    //
    // Compute number of required extensions
    //
    uint32_t ext_count = 0;

    for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(rs_header_data.header->extensions.bitmap); ii++)
      {
        ext_count += __builtin_popcount(rs_header_data.header->extensions.bitmap[ii]);
      }

    if (requirements->ext_names == NULL)
      {
        requirements->ext_name_count = ext_count;

        if (ext_count > 0)
          {
            is_ok = false;
          }
      }
    else
      {
        if (requirements->ext_name_count < ext_count)
          {
            is_ok = false;
          }
        else
          {
            //
            // FIXME(allanmac): This can be accelerated by exploiting
            // the extension bitmap.
            //
            uint32_t ii = 0;

#define RS_VK_TARGET_EXTENSION_STRING(ext_) "VK_" STRINGIFY_MACRO(ext_)

#undef RS_VK_TARGET_EXTENSION
#define RS_VK_TARGET_EXTENSION(ext_)                                                               \
  if (rs_header_data.header->extensions.named.ext_)                                                \
    {                                                                                              \
      requirements->ext_names[ii++] = RS_VK_TARGET_EXTENSION_STRING(ext_);                         \
    }

            RS_VK_TARGET_EXTENSIONS()
          }
      }
  }

  //
  // Enable physical device features
  //
  if ((requirements->pdf == NULL) || (requirements->pdf11 == NULL) || (requirements->pdf12 == NULL))
    {
      is_ok = false;
    }
  else
    {
#undef RS_VK_TARGET_FEATURE_VK10
#define RS_VK_TARGET_FEATURE_VK10(feature_) 1 +

#undef RS_VK_TARGET_FEATURE_VK11
#define RS_VK_TARGET_FEATURE_VK11(feature_) 1 +

#undef RS_VK_TARGET_FEATURE_VK12
#define RS_VK_TARGET_FEATURE_VK12(feature_) 1 +

      //
      // Don't create the variable if it's not used
      //
#if (RS_VK_TARGET_FEATURES_VK10() 0)
      VkPhysicalDeviceFeatures * const pdf = requirements->pdf;
#endif
#if (RS_VK_TARGET_FEATURES_VK11() 0)
      VkPhysicalDeviceVulkan11Features * const pdf11 = requirements->pdf11;
#endif
#if (RS_VK_TARGET_FEATURES_VK12() 0)
      VkPhysicalDeviceVulkan12Features * const pdf12 = requirements->pdf12;
#endif

      //
      // Let's always have this on during debug
      //
#ifndef NDEBUG
      pdf->robustBufferAccess = true;
#endif

      //
      // VULKAN 1.0
      //
#undef RS_VK_TARGET_FEATURE_VK10
#define RS_VK_TARGET_FEATURE_VK10(feature_)                                                        \
  if (rs_header_data.header->features.named.feature_)                                              \
    pdf->feature_ = true;

      RS_VK_TARGET_FEATURES_VK10()

      //
      // VULKAN 1.1
      //
#undef RS_VK_TARGET_FEATURE_VK11
#define RS_VK_TARGET_FEATURE_VK11(feature_)                                                        \
  if (rs_header_data.header->features.named.feature_)                                              \
    pdf11->feature_ = true;

      RS_VK_TARGET_FEATURES_VK11()

      //
      // VULKAN 1.2
      //
#undef RS_VK_TARGET_FEATURE_VK12
#define RS_VK_TARGET_FEATURE_VK12(feature_)                                                        \
  if (rs_header_data.header->features.named.feature_)                                              \
    pdf12->feature_ = true;

      RS_VK_TARGET_FEATURES_VK12()
    }

  //
  //
  //
  return is_ok;
}

//
//
//
