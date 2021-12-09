// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_FUCHSIA_KEY_TO_HID_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_FUCHSIA_KEY_TO_HID_H_

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
// Converts a Fuchsia key to a HID key.
//
// May return 0 if the map doesn't support the Fuchsia key.
//

uint32_t
surface_fuchsia_key_to_hid(uint32_t key);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_SURFACE_FUCHSIA_KEY_TO_HID_H_
