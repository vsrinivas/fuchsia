// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_EXT_FIND_TARGET_FIND_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_EXT_FIND_TARGET_FIND_TARGET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "spinel/platforms/vk/spinel_vk_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Returns optimal target for a vendor/device id pair.
//
// Otherwise, returns NULL.
//
spinel_vk_target_t *
spinel_vk_find_target(uint32_t vendor_id, uint32_t device_id);

//
// Disposes the target.
//
// When the targets are linkable, this is a noop.
//
void
spinel_vk_target_dispose(spinel_vk_target_t * target);

//
//
//

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_EXT_FIND_TARGET_FIND_TARGET_H_
