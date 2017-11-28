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

// We define that we're ready to pave if we have
//  - a partition with a ChromeOS kernel GUID which is at least sz_kern bytes
//  - a partition with the ChromeOS root GUID which is at least sz_root bytes
//  - IF fvm_req is true then
//    * EITHER
//      # - a partition with the Fuchsia FVM GUID
//                      OR
//      # - the size of the STATE partition is already minimized, meaning we've
//          the disk has as much free space as possible already for the FVM
bool is_ready_to_pave(const gpt_device_t* gpt, const block_info_t* block_info,
                      const uint64_t sz_kern, const uint64_t sz_root,
                      const bool fvm_req);

// Configure the GPT for a dual-boot of Fuchsia and ChromeOS. The kern-c and
// root-c partitions of the device will be configured such that they match the
// requested sizes.
//
// If fvm_req is true this attempts to configure the disk is a compatible way.
// If the disk already has an FVM, no alterations are made to it and no further
// reconfiguration of the disk is considered necessary. If an FVM is not present
// the STATE partition of the disk will be minimized to make as much room as
// possible for the FVM.
//
// If fvm_req is false the function will neither consider if an FVM partition
// exists nor whether there is available space for one to be added.
//
// Returns ZX_OK if reconfiguration succeeds and then the GPT should be
// persisted. Returns ZX_ERR_BAD_STATE if the partition table can't be
// reconfigured. In the case of error, the GPT should NOT be written back to
// disk and should be discarded.
zx_status_t config_cros_for_fuchsia(gpt_device_t* gpt,
                                    const block_info_t* blk_info,
                                    const uint64_t sz_kern,
                                    const uint64_t sz_root,
                                    const bool fvm_req);

__END_CDECLS
