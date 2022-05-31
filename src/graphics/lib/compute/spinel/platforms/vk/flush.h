// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_FLUSH_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_FLUSH_H_

//
//
//

#include "device.h"

//
// Flush a noncoherent mapped ring
//

void
spinel_ring_flush(struct spinel_device_vk const * vk,
                  VkDeviceMemory                  ring_dm,
                  VkDeviceSize                    ring_dm_offset,
                  uint32_t                        ring_size,
                  uint32_t                        ring_head,
                  uint32_t                        ring_span,
                  VkDeviceSize                    ring_elem_size);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_FLUSH_H_
