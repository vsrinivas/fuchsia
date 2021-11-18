// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Define header for radix sort target.
//

#include "config.h"
#include "target.h"

//
//
//

static struct radix_sort_vk_target_header const header __attribute__((used)) =
{
  .magic = RS_HEADER_MAGIC,

  .extensions.named = {
    .EXT_subgroup_size_control = 1,
  },

  .features.named = {
    .shaderInt16                  = 1,
    .shaderInt64                  = 1,
    .bufferDeviceAddress          = 1,
    .vulkanMemoryModel            = 1,
    .vulkanMemoryModelDeviceScope = 1,
  },

  .config = {
    .keyval_dwords         = RS_KEYVAL_DWORDS,

    .histogram =  {
      .workgroup_size_log2 = RS_HISTOGRAM_WORKGROUP_SIZE_LOG2,
      .subgroup_size_log2  = RS_HISTOGRAM_SUBGROUP_SIZE_LOG2,
      .block_rows          = RS_HISTOGRAM_BLOCK_ROWS
    },

    .prefix =  {
      .workgroup_size_log2 = RS_PREFIX_WORKGROUP_SIZE_LOG2,
      .subgroup_size_log2  = RS_PREFIX_SUBGROUP_SIZE_LOG2
    },

    .scatter = {
      .workgroup_size_log2 = RS_SCATTER_WORKGROUP_SIZE_LOG2,
      .subgroup_size_log2  = RS_SCATTER_SUBGROUP_SIZE_LOG2,
      .block_rows          = RS_SCATTER_BLOCK_ROWS
    }
  }
};

//
//
//
