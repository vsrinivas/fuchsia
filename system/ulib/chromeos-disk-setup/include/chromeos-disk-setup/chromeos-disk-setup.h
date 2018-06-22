// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <gpt/gpt.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

// Recommended default size of the Zircon kernel partition
#define SZ_ZX_PART (64 * ((uint64_t)1) << 20)
// Recommended default size of the root partition
#define SZ_ROOT_PART (4 * ((uint64_t)1) << 30)
// Recommended minimum size of the ChromeOS state partition
#define MIN_SZ_STATE (5 * ((uint64_t)1) << 30)

__BEGIN_CDECLS

// determine if this looks like a ChromeOS partition layout
bool is_cros(const gpt_device_t* gpt);

// We define that we're ready to pave if
//  - ZIRCON-A, ZIRCON-B, and ZIRCON-R are present and at least sz_kern bytes
//  - An FVM a partition is present
bool is_ready_to_pave(const gpt_device_t* gpt, const block_info_t* block_info,
                      const uint64_t sz_kern);

// Configure the GPT for a dual-boot of Fuchsia and ChromeOS.
//
// Partitions ZIRCON-A, ZIRCON-B, ZIRCON-R, and FVM will be created.
//
// If space is required to create the above partitions, KERN-C and ROOT-C may be
// deleted, and STATE may be resized.
//
// Returns ZX_OK if reconfiguration succeeds and then the GPT should be
// persisted. Returns ZX_ERR_BAD_STATE if the partition table can't be
// reconfigured. In the case of error, the GPT should NOT be written back to
// disk and should be discarded.
zx_status_t config_cros_for_fuchsia(gpt_device_t* gpt,
                                    const block_info_t* blk_info,
                                    const uint64_t sz_kern);

__END_CDECLS
