// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

struct spn_device;

//
//
//

void
spn_device_block_pool_create(struct spn_device * const device,
                             uint64_t            const block_pool_size,
                             uint32_t            const handle_count);

void
spn_device_block_pool_dispose(struct spn_device * const device);

//
//
//

uint32_t
spn_device_block_pool_get_mask(struct spn_device * const device);

//
//
//

struct spn_target_ds_block_pool_t
spn_device_block_pool_get_ds(struct spn_device * const device);

//
//
//
