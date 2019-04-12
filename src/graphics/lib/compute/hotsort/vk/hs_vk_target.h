// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <stdint.h>

#include "common/macros.h"

//
// This structure packages target-specific HotSort parameters and
// SPIR-V modules.
//

struct hs_vk_target_config
{
  struct {
    uint8_t   threads_log2;
    uint8_t   width_log2;
    uint8_t   height;
  } slab;

  struct {
    uint8_t   key;
    uint8_t   val;
  } dwords;

  struct {
    uint8_t   slabs;
  } block;

  struct {
    struct {
      uint8_t scale_min;
      uint8_t scale_max;
    } fm;
    struct {
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

struct hs_vk_target
{
  struct hs_vk_target_config config;
  ALIGN_MACRO(4) uint8_t     modules[]; // modules[] must start on 32-bit boundary
};

//
//
//
