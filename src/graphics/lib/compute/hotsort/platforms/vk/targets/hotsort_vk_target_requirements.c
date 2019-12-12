// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hotsort_vk_target_requirements.h"

#include <string.h>

#include "common/macros.h"
#include "hotsort_vk.h"
#include "hotsort_vk_target.h"

//
// TARGET PROPERTIES: VULKAN
//
// Yields the extensions and features required by a HotSort target.
//
// If target is not NULL and requirements.ext_names is NULL then the
// respective count will be initialized.
//
// Returns false if:
//
//   * target is NULL
//   * requirements is NULL
//   * requirements.ext_names is NULL
//   * requirements.pdf is NULL
//   * requirements.ext_name_count is too small
//
// Otherwise returns true.
//
bool
hotsort_vk_target_get_requirements(struct hotsort_vk_target const * const        target,
                                   struct hotsort_vk_target_requirements * const requirements)
{
  //
  //
  //
  if ((target == NULL) || (requirements == NULL))
    {
      return false;
    }

  //
  // REQUIRED EXTENSIONS
  //
  bool is_success = true;

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

      if (ext_count > 0)
        {
          is_success = false;
        }
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

#define HOTSORT_VK_TARGET_EXTENSION_STRING(ext_) "VK_" STRINGIFY_MACRO(ext_)

#undef HOTSORT_VK_TARGET_EXTENSION
#define HOTSORT_VK_TARGET_EXTENSION(ext_)                                                          \
  if (target->config.extensions.named.ext_)                                                        \
    {                                                                                              \
      requirements->ext_names[ii++] = HOTSORT_VK_TARGET_EXTENSION_STRING(ext_);                    \
    }

          HOTSORT_VK_TARGET_EXTENSIONS()
        }
    }

  //
  // REQUIRED FEATURES
  //
  if (requirements->pdf == NULL)
    {
      is_success = false;
    }
  else
    {
      //
      // Let's always turn this on during debug
      //
#ifndef NDEBUG
      requirements->pdf->robustBufferAccess = true;
#endif

      //
      // Enable target features
      //
#undef HOTSORT_VK_TARGET_FEATURE
#define HOTSORT_VK_TARGET_FEATURE(feature_)                                                        \
  if (target->config.features.named.feature_)                                                      \
    requirements->pdf->feature_ = true;

      HOTSORT_VK_TARGET_FEATURES()
    }

  return is_success;
}

//
//
//
