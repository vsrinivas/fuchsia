// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hw/sdhci.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct sdhci_protocol_ops {
    // TODO: should be replaced with a generic busdev mechanism
    mx_handle_t (*get_interrupt)(void* ctx);
    mx_status_t (*get_mmio)(void* ctx, volatile sdhci_regs_t** out);
    uint32_t (*get_base_clock)(void* ctx);
    // TODO: replace this function with iotxn_phys(txn, ctx)
    mx_paddr_t (*get_dma_offset)(void* ctx);

    // returns device quirks
    uint64_t (*get_quirks)(void* ctx);
    // platform specific HW reset
    void (*hw_reset)(void* ctx);
} sdhci_protocol_ops_t;

// This is a BCM28xx specific quirk. The bottom 8 bits of the 136
// bit response are normally filled by 7 CRC bits and 1 reserved bit.
// The BCM controller checks the CRC for us and strips it off in the
// process.
// The higher level stack expects 136B responses to be packed in a
// certain way so we shift all the fields back to their proper offsets.
#define SDHCI_QUIRK_STRIP_RESPONSE_CRC (1 << 0)
// BCM28xx quirk: The BCM28xx appears to use its internal DMA engine to
// perform transfers against the SD card. Normally we would use SDMA or
// ADMA (if the part supported it). Since this part doesn't appear to
// support either, we just use PIO.
#define SDHCI_QUIRK_NO_DMA             (1 << 1)

typedef struct sdhci_protocol {
    sdhci_protocol_ops_t* ops;
    void* ctx;
} sdhci_protocol_t;

__END_CDECLS;
