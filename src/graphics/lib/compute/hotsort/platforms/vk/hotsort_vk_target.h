// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_TARGET_H_

//
//
//

#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// This structure packages target-specific HotSort parameters and
// SPIR-V modules.
//

struct hotsort_vk_target_config
{
  uint8_t is_in_place;

  struct
  {
    uint8_t threads_log2;
    uint8_t width_log2;
    uint8_t height;
  } slab;

  struct
  {
    uint8_t key;
    uint8_t val;
  } dwords;

  struct
  {
    uint8_t slabs;
  } block;

  struct
  {
    struct
    {
      uint8_t scale_min;
      uint8_t scale_max;
    } fm;
    struct
    {
      uint8_t scale_min;
      uint8_t scale_max;
    } hm;
  } merge;
};

//
// For now, kernels are appended end-to-end with a leading big-endian
// length followed by a SPIR-V binary.
//
// The entry point for each kernel is "main".
//
// When the tools support packaging multiple named compute shaders in
// one SPIR-V module then reevaluate this encoding.
//

struct hotsort_vk_target
{
  struct hotsort_vk_target_config config;
  uint32_t                        modules[];
};

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_TARGET_H_
