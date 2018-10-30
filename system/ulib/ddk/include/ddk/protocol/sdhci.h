// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdhci.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef uint64_t sdhci_quirk_t;
// This is a BCM28xx specific quirk. The bottom 8 bits of the 136
// bit response are normally filled by 7 CRC bits and 1 reserved bit.
// The BCM controller checks the CRC for us and strips it off in the
// process.
// The higher level stack expects 136B responses to be packed in a
// certain way so we shift all the fields back to their proper offsets.
#define SDHCI_QUIRK_STRIP_RESPONSE_CRC UINT64_C(1)
// BCM28xx quirk: The BCM28xx appears to use its internal DMA engine to
// perform transfers against the SD card. Normally we would use SDMA or
// ADMA (if the part supported it). Since this part doesn't appear to
// support either, we just use PIO.
#define SDHCI_QUIRK_NO_DMA UINT64_C(2)
// The bottom 8 bits of the 136 bit response are normally filled by 7 CRC bits
// and 1 reserved bit. Some controllers strip off the CRC.
// The higher level stack expects 136B responses to be packed in a certain way
// so we shift all the fields back to their proper offsets.
#define SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER UINT64_C(4)

typedef struct sdhci_protocol sdhci_protocol_t;

// Declarations

typedef struct sdhci_protocol_ops {
    zx_status_t (*get_interrupt)(void* ctx, zx_handle_t* out_irq);
    zx_status_t (*get_mmio)(void* ctx, zx_handle_t* out_mmio);
    zx_status_t (*get_bti)(void* ctx, uint32_t index, zx_handle_t* out_bti);
    uint32_t (*get_base_clock)(void* ctx);
    uint64_t (*get_quirks)(void* ctx);
    void (*hw_reset)(void* ctx);
} sdhci_protocol_ops_t;

struct sdhci_protocol {
    sdhci_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t sdhci_get_interrupt(const sdhci_protocol_t* proto, zx_handle_t* out_irq) {
    return proto->ops->get_interrupt(proto->ctx, out_irq);
}
static inline zx_status_t sdhci_get_mmio(const sdhci_protocol_t* proto, zx_handle_t* out_mmio) {
    return proto->ops->get_mmio(proto->ctx, out_mmio);
}
// Gets a handle to the bus transaction initiator for the device. The caller
// receives ownership of the handle.
static inline zx_status_t sdhci_get_bti(const sdhci_protocol_t* proto, uint32_t index,
                                        zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, index, out_bti);
}
static inline uint32_t sdhci_get_base_clock(const sdhci_protocol_t* proto) {
    return proto->ops->get_base_clock(proto->ctx);
}
// returns device quirks
static inline uint64_t sdhci_get_quirks(const sdhci_protocol_t* proto) {
    return proto->ops->get_quirks(proto->ctx);
}
// platform specific HW reset
static inline void sdhci_hw_reset(const sdhci_protocol_t* proto) {
    proto->ops->hw_reset(proto->ctx);
}

__END_CDECLS;
