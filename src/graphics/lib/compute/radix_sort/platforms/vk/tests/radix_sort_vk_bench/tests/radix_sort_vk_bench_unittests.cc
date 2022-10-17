// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "radix_sort_vk_bench.h"

//
//
//
namespace radix_sort::vk::tests {

//
//
//
TEST(RadixSortVK, Direct_U32_Range_0_to_0)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 1,
    .is_direct      = true,
    .is_indirect    = false,
    .count_lo       = 0,
    .count_hi       = 0,
    .count_step     = 1,
    .loops          = 1,
    .warmup         = 1,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_FAILURE);
}

//
//
//
TEST(RadixSortVK, Direct_U32_Range_0_to_8)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 1,
    .is_direct      = true,
    .is_indirect    = false,
    .count_lo       = 0,
    .count_hi       = 8,
    .count_step     = 1,
    .loops          = 1,
    .warmup         = 1,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Indirect_U32_Range_0_to_8)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 1,
    .is_direct      = false,
    .is_indirect    = true,
    .count_lo       = 0,
    .count_hi       = 8,
    .count_step     = 1,
    .loops          = 1,
    .warmup         = 1,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Direct_U32_Range_256K_to_1M)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 1,
    .is_direct      = true,
    .is_indirect    = false,
    .count_lo       = 1u << 18,
    .count_hi       = 1u << 20,
    .count_step     = 1u << 18,
    .loops          = 1,
    .warmup         = 0,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Indirect_U32_Range_256K_to_1M)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 1,
    .is_direct      = false,
    .is_indirect    = true,
    .count_lo       = 1u << 18,
    .count_hi       = 1u << 20,
    .count_step     = 1u << 18,
    .loops          = 1,
    .warmup         = 0,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Direct_U64_Range_0_to_8)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 2,
    .is_direct      = true,
    .is_indirect    = false,
    .count_lo       = 0,
    .count_hi       = 8,
    .count_step     = 1,
    .loops          = 1,
    .warmup         = 1,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Indirect_U64_Range_0_to_8)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 2,
    .is_direct      = false,
    .is_indirect    = true,
    .count_lo       = 0,
    .count_hi       = 8,
    .count_step     = 1,
    .loops          = 1,
    .warmup         = 1,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Direct_U64_Range_256K_to_1M)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 2,
    .is_direct      = true,
    .is_indirect    = false,
    .count_lo       = 1u << 18,
    .count_hi       = 1u << 20,
    .count_step     = 1u << 18,
    .loops          = 1,
    .warmup         = 0,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//
TEST(RadixSortVK, Indirect_U64_Range_256K_to_1M)
{
  struct radix_sort_vk_bench_info info = {
    .exec_name      = "radix_sort_vk_bench",
    .vendor_id      = 0,
    .device_id      = 0,
    .target_name    = NULL,
    .keyval_dwords  = 2,
    .is_direct      = false,
    .is_indirect    = true,
    .count_lo       = 1u << 18,
    .count_hi       = 1u << 20,
    .count_step     = 1u << 18,
    .loops          = 1,
    .warmup         = 0,
    .is_verify      = true,
    .is_validation  = true,
    .is_debug_utils = true,
    .is_verbose     = false,
  };

  EXPECT_EQ(radix_sort_vk_bench(&info), EXIT_SUCCESS);
}

//
//
//

}  // namespace radix_sort::vk::tests

//
//
//
