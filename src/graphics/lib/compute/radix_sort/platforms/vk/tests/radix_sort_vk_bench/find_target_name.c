// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "find_target_name.h"

#include <stddef.h>

//
// Construct a target name.
//
// clang-format off
#define RS_VK_TARGET_STRINGIFY_2(name_) #name_
#define RS_VK_TARGET_STRINGIFY(name_)   RS_VK_TARGET_STRINGIFY_2(name_)

#if defined(__Fuchsia__) && !defined(RS_VK_TARGET_ARCHIVE_LINKABLE)
#define RS_VK_TARGET_NAME(name_) "pkg/data/targets/radix_sort_vk_" RS_VK_TARGET_STRINGIFY(name_) "_resource.ar"
#else
#define RS_VK_TARGET_NAME(name_) RS_VK_TARGET_STRINGIFY(name_)
#endif
// clang-format on

//
// Returns optimal target name for a { vendor id, device id, keyval dwords }
// triple.
//
// Otherwise, returns NULL.
//
char const *
radix_sort_vk_find_target_name(uint32_t vendor_id, uint32_t device_id, uint32_t keyval_dwords)
{
  switch (vendor_id)
    {
      case 0x10DE:
        //
        // NVIDIA
        //
        switch (keyval_dwords)
          {
            case 1:
              return RS_VK_TARGET_NAME(nvidia_sm35_u32);
            case 2:
              return RS_VK_TARGET_NAME(nvidia_sm35_u64);
          }

      case 0x1002:
        //
        // AMD
        //
        switch (keyval_dwords)
          {
            case 1:
              return RS_VK_TARGET_NAME(amd_gcn3_u32);
            case 2:
              return RS_VK_TARGET_NAME(amd_gcn3_u64);
          }

      case 0x8086:
        //
        // INTEL
        //
        switch (keyval_dwords)
          {
            case 1:
              return RS_VK_TARGET_NAME(intel_gen8_u32);
            case 2:
              return RS_VK_TARGET_NAME(intel_gen8_u64);
          }

      case 0x13B5:
        //
        // ARM MALI
        //
        switch (device_id)
          {
            case 0x70930000:
              //
              // BIFROST4
              //
              switch (keyval_dwords)
                {
                  case 1:
                    return RS_VK_TARGET_NAME(arm_bifrost4_u32);
                  case 2:
                    return RS_VK_TARGET_NAME(arm_bifrost4_u64);
                }

            case 0x72120000:
              //
              // BIFROST8
              //
              switch (keyval_dwords)
                {
                  case 1:
                    return RS_VK_TARGET_NAME(arm_bifrost8_u32);
                  case 2:
                    return RS_VK_TARGET_NAME(arm_bifrost8_u64);
                }
          }
    }

  return NULL;
}

//
//
//
