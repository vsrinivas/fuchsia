// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/interrupt.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

zx_status_t arm_gicv2m_msi_init();

// Since ARM determines which GIC is used at runtime, this header sets up the gicv2m instances
// of the MSI functions so they can be passed into the pbus interrupt function table, which
// fulfills the interface specified in dev/interrupt.h.
bool arm_gicv2m_msi_is_supported();
bool arm_gicv2m_msi_supports_masking();
void arm_gicv2m_msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask);
zx_status_t arm_gicv2m_msi_alloc_block(uint requested_irqs,
                                       bool can_target_64bit,
                                       bool is_msix,
                                       msi_block_t* out_block);
void arm_gicv2m_msi_free_block(msi_block_t* block);
void arm_gicv2m_msi_register_handler(const msi_block_t* block,
                                     uint msi_id,
                                     int_handler handler,
                                     void* ctx);
