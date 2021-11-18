// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_H_

//
//
//

#include <stdint.h>

#include "target_requirements.h"

//
//
//

#define RS_HEADER_MAGIC 0x4B565352  // "RSVK"

//
// This structure packages target-specific configuration parameters.
//

struct radix_sort_vk_target_config
{
  uint32_t keyval_dwords;

  struct
  {
    uint32_t workgroup_size_log2;
  } init;

  struct
  {
    uint32_t workgroup_size_log2;
  } fill;

  struct
  {
    uint32_t workgroup_size_log2;
    uint32_t subgroup_size_log2;
    uint32_t block_rows;
  } histogram;

  struct
  {
    uint32_t workgroup_size_log2;
    uint32_t subgroup_size_log2;
  } prefix;

  struct
  {
    uint32_t workgroup_size_log2;
    uint32_t subgroup_size_log2;
    uint32_t block_rows;
  } scatter;
};

//
//
//

struct radix_sort_vk_target_header
{
  uint32_t                              magic;       // magic header dword
  union radix_sort_vk_target_extensions extensions;  // target device extensions
  union radix_sort_vk_target_features   features;    // target device features
  struct radix_sort_vk_target_config    config;      // target configuration
  uint32_t                              modules[];   // SPIR-V modules
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGET_H_
