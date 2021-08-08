// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TOOLS_BENCH_PLATFORMS_VK_BENCH_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TOOLS_BENCH_PLATFORMS_VK_BENCH_VK_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Failure if EXIT_SUCCESS isn't returned
//
int
bench_vk(uint32_t argc, char const * argv[]);

//
// Usage to stderr
//
void
bench_vk_usage(char const * argv[]);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TOOLS_BENCH_PLATFORMS_VK_BENCH_VK_H_
