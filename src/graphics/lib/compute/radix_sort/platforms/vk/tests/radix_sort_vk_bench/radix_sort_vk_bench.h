// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_RADIX_SORT_VK_BENCH_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_RADIX_SORT_VK_BENCH_H_

//
//
//

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Benchmark info
//
// Note that (`.is_direct` xor `.is_indirect`) must be true as an indication
// that either the "direct" or "indirect" token appeared in the parsed
// arguments.
//
struct radix_sort_vk_bench_info
{
  char const * exec_name;       //
  uint32_t     vendor_id;       // If vendor:device is 0:0 then use first VkPhysicalDevice.
  uint32_t     device_id;       //
  char const * target_name;     // If NULL then find target name.
  uint32_t     keyval_dwords;   // Only eval'd if target_name is NULL.
  bool         is_direct;       // Directly dispatch shaders.  Implies `.is_indirect` is false.
  bool         is_indirect;     // Indirectly dispatch shaders.  Implies `.is_direct` is false.
  uint32_t     count_lo;        // Starting number of keyvals.
  uint32_t     count_hi;        // Ending number of keyvals.
  uint32_t     count_step;      // Increase count until greater than `.count_hi`.
  uint32_t     loops;           // Number of times to execute the benchmarked sort.
  uint32_t     warmup;          // Number of times to execute the sort before benchmarking.
  uint32_t     is_verify;       // Compare GPU to CPU sorted output.
  bool         is_validation;   // Load Vulkan Validation Layers.
  bool         is_debug_utils;  // Label Vulkan objects.
  bool         is_verbose;      // Verbose output.
};

//
// Parse args as defined in `rs_usage()`.
//
// Parsing always succeeds.
//
// The `radix_sort_vk_bench()` will further validate the `info` struct.
//
void
radix_sort_vk_bench_parse(int argc, char const * argv[], struct radix_sort_vk_bench_info * info);

//
// Execute benchmark.
//
// Failure if EXIT_FAILURE is returned.
//
// Otherwise, returns EXIT_SUCCESS.
//
int
radix_sort_vk_bench(struct radix_sort_vk_bench_info const * info);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_RADIX_SORT_VK_BENCH_H_
