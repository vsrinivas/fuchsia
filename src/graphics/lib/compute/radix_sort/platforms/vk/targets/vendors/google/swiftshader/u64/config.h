// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGETS_VENDORS_GOOGLE_SWIFTSHADER_U64_CONFIG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGETS_VENDORS_GOOGLE_SWIFTSHADER_U64_CONFIG_H_

//
//
//

// clang-format off
#define RS_KEYVAL_DWORDS                   2

#define RS_FILL_WORKGROUP_SIZE_LOG2        2
#define RS_FILL_BLOCK_ROWS                 12

#define RS_HISTOGRAM_WORKGROUP_SIZE_LOG2   2
#define RS_HISTOGRAM_SUBGROUP_SIZE_LOG2    2
#define RS_HISTOGRAM_BLOCK_ROWS            16

#define RS_PREFIX_WORKGROUP_SIZE_LOG2      2
#define RS_PREFIX_SUBGROUP_SIZE_LOG2       2

#define RS_SCATTER_WORKGROUP_SIZE_LOG2     5
#define RS_SCATTER_SUBGROUP_SIZE_LOG2      2
#define RS_SCATTER_BLOCK_ROWS              16
// clang-format on

//
// - No support for .shaderInt64
// - Don't accumulate the histogram in shared memory
// - Skip reordering of scatter block in shared memory
// - Use a broadcast match
// - Workgroups aren't sequentially dispatched
//
#define RS_DISABLE_SHADER_INT64
#define RS_HISTOGRAM_DISABLE_SMEM_HISTOGRAM
#define RS_SCATTER_DISABLE_REORDER
#define RS_SCATTER_ENABLE_BROADCAST_MATCH
#define RS_SCATTER_NONSEQUENTIAL_DISPATCH

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TARGETS_VENDORS_GOOGLE_SWIFTSHADER_U64_CONFIG_H_
