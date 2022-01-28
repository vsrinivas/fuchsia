// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "target_requirements.h"

#include <stdio.h>

#include "common/macros.h"
#include "radix_sort/platforms/vk/radix_sort_vk.h"
#include "spinel/platforms/vk/spinel_vk.h"
#include "target.h"
#include "target_archive/target_archive.h"

//
//
//
struct spinel_vk_target
{
  struct target_archive_header ar_header;
};

//
// SPINEL TARGET REQUIREMENTS: VULKAN
//
bool
spinel_vk_target_get_requirements(struct spinel_vk_target const *        target,
                                  struct spinel_vk_target_requirements * requirements)
{
  //
  // Must not be NULL
  //
  if ((target == NULL) || (requirements == NULL))
    {
      return false;
    }

#ifndef SPN_VK_TARGET_DISABLE_VERIFY
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
  // Get the target header
  //
  struct target_archive_header const * const ar_header  = &target->ar_header;
  struct target_archive_entry const * const  ar_entries = ar_header->entries;
  uint32_t const * const                     ar_data    = ar_entries[ar_header->count - 1].data;

  //
  // Get the spinel target header
  //
  union
  {
    struct spinel_target_header const * header;
    uint32_t const *                    data;
  } const spinel_header_data = { .data = ar_data };

  //
  // Get the embedded radix_vk_target
  //
  union
  {
    struct radix_sort_vk_target const * target;
    uint32_t const *                    data;
  } const rs_target_data = { .data = ar_data + (ar_entries[ar_header->count - 1].offset >> 2) };

  //
  // Verify target is compatible with the library.
  //
#ifndef SPN_VK_TARGET_DISABLE_VERIFY
  if (spinel_header_data.header->magic != SPN_HEADER_MAGIC)
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
  bool     is_ok          = true;
  uint32_t ext_name_count = 0;

  //
  // EXTENSIONS
  //
  // Compute number of required extensions
  //
  for (uint32_t ii = 0; ii < ARRAY_LENGTH_MACRO(spinel_header_data.header->extensions.bitmap); ii++)
    {
      ext_name_count += __builtin_popcount(spinel_header_data.header->extensions.bitmap[ii]);
    }

  if (requirements->ext_names == NULL)
    {
      requirements->ext_name_count = ext_name_count;

      if (ext_name_count > 0)
        {
          is_ok = false;
        }
    }
  else
    {
      if (requirements->ext_name_count < ext_name_count)
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

#define SPN_TARGET_EXTENSION_STRING(ext_) "VK_" STRINGIFY_MACRO(ext_)

#undef SPN_TARGET_EXTENSION
#define SPN_TARGET_EXTENSION(ext_)                                                                 \
  if (spinel_header_data.header->extensions.named.ext_)                                            \
    {                                                                                              \
      requirements->ext_names[ii++] = SPN_TARGET_EXTENSION_STRING(ext_);                           \
    }

          SPN_TARGET_EXTENSIONS()
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
#undef SPN_TARGET_FEATURE_VK10
#define SPN_TARGET_FEATURE_VK10(feature_) 1 +

#undef SPN_TARGET_FEATURE_VK11
#define SPN_TARGET_FEATURE_VK11(feature_) 1 +

#undef SPN_TARGET_FEATURE_VK12
#define SPN_TARGET_FEATURE_VK12(feature_) 1 +

      //
      // Don't create the variable if it's not used
      //
#if (SPN_TARGET_FEATURES_VK10() 0)
      VkPhysicalDeviceFeatures * const pdf = requirements->pdf;
#endif
#if (SPN_TARGET_FEATURES_VK11() 0)
      VkPhysicalDeviceVulkan11Features * const pdf11 = requirements->pdf11;
#endif
#if (SPN_TARGET_FEATURES_VK12() 0)
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
#undef SPN_TARGET_FEATURE_VK10
#define SPN_TARGET_FEATURE_VK10(feature_)                                                          \
  if (spinel_header_data.header->features.named.feature_)                                          \
    {                                                                                              \
      pdf->feature_ = true;                                                                        \
    }

      SPN_TARGET_FEATURES_VK10()

      //
      // VULKAN 1.1
      //
#undef SPN_TARGET_FEATURE_VK11
#define SPN_TARGET_FEATURE_VK11(feature_)                                                          \
  if (spinel_header_data.header->features.named.feature_)                                          \
    {                                                                                              \
      pdf11->feature_ = true;                                                                      \
    }

      SPN_TARGET_FEATURES_VK11()

      //
      // VULKAN 1.2
      //
#undef SPN_TARGET_FEATURE_VK12
#define SPN_TARGET_FEATURE_VK12(feature_)                                                          \
  if (spinel_header_data.header->features.named.feature_)                                          \
    {                                                                                              \
      pdf12->feature_ = true;                                                                      \
    }

      SPN_TARGET_FEATURES_VK12()
    }

  //
  // Concatenate radix sort requirements
  //
  if (requirements->ext_names == NULL)
    {
      struct radix_sort_vk_target_requirements rs_tr = {
        // .ext_name_count = 0,
        // .ext_names      = NULL
        .pdf   = requirements->pdf,
        .pdf11 = requirements->pdf11,
        .pdf12 = requirements->pdf12,
      };

      bool const rs_is_ok = radix_sort_vk_target_get_requirements(rs_target_data.target, &rs_tr);
      is_ok               = is_ok && rs_is_ok;

      requirements->ext_name_count += rs_tr.ext_name_count;
    }
  else
    {
      uint32_t const rs_ext_name_count = (requirements->ext_name_count > ext_name_count)
                                           ? (requirements->ext_name_count - ext_name_count)
                                           : 0;

      struct radix_sort_vk_target_requirements rs_tr = {
        .ext_name_count = rs_ext_name_count,
        .ext_names      = requirements->ext_names + ext_name_count,
        .pdf            = requirements->pdf,
        .pdf11          = requirements->pdf11,
        .pdf12          = requirements->pdf12,
      };

      bool const rs_is_ok = radix_sort_vk_target_get_requirements(rs_target_data.target, &rs_tr);
      is_ok               = is_ok && rs_is_ok;
    }

  //
  //
  //
  return is_ok;
}

//
//
//
