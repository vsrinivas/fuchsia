// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_FIND_TARGET_NAME_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_FIND_TARGET_NAME_H_

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
// Returns optimal target name for a { vendor id, device id, keyval dwords }
// triple.
//
// Otherwise, returns NULL.
//
char const *
radix_sort_vk_find_target_name(uint32_t vendor_id, uint32_t device_id, uint32_t keyval_dwords);

//
//
//

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_TESTS_RADIX_SORT_VK_BENCH_FIND_TARGET_NAME_H_
