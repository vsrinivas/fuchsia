// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STATUS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STATUS_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_result.h"
#include "spinel_vk_types.h"
#include "vk.h"

//
//
//

struct spn_device;

//
//
//

void
spn_device_status_create(struct spn_device * const device);

void
spn_device_status_dispose(struct spn_device * const device);

//
//
//

spn_result_t
spn_device_get_status(struct spn_device * const device, spn_status_t const * status);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_STATUS_H_
