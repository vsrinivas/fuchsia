// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_dsp.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct ihda_dsp_irq ihda_dsp_irq_t;
typedef struct ihda_dsp_protocol ihda_dsp_protocol_t;

// Declarations

#define MD_KEY_NHLT "NHLT"

struct ihda_dsp_irq {
    void (*callback)(void* ctx);
    void* ctx;
};

typedef struct ihda_dsp_protocol_ops {
    void (*get_dev_info)(void* ctx, zx_pcie_device_info_t* out_out);
    zx_status_t (*get_mmio)(void* ctx, zx_handle_t* out_vmo, size_t* out_size);
    zx_status_t (*get_bti)(void* ctx, zx_handle_t* out_bti);
    void (*enable)(void* ctx);
    void (*disable)(void* ctx);
    zx_status_t (*irq_enable)(void* ctx, const ihda_dsp_irq_t* callback);
    void (*irq_disable)(void* ctx);
} ihda_dsp_protocol_ops_t;

struct ihda_dsp_protocol {
    ihda_dsp_protocol_ops_t* ops;
    void* ctx;
};

// Fetch the parent HDA controller's PCI device info.
static inline void ihda_dsp_get_dev_info(const ihda_dsp_protocol_t* proto,
                                         zx_pcie_device_info_t* out_out) {
    proto->ops->get_dev_info(proto->ctx, out_out);
}
// Fetch a VMO that represents the BAR holding the Audio DSP registers.
static inline zx_status_t ihda_dsp_get_mmio(const ihda_dsp_protocol_t* proto, zx_handle_t* out_vmo,
                                            size_t* out_size) {
    return proto->ops->get_mmio(proto->ctx, out_vmo, out_size);
}
// Fetch a handle to our bus transaction initiator.
static inline zx_status_t ihda_dsp_get_bti(const ihda_dsp_protocol_t* proto, zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, out_bti);
}
// Enables DSP
static inline void ihda_dsp_enable(const ihda_dsp_protocol_t* proto) {
    proto->ops->enable(proto->ctx);
}
// Disable DSP
static inline void ihda_dsp_disable(const ihda_dsp_protocol_t* proto) {
    proto->ops->disable(proto->ctx);
}
// Enables DSP interrupts and set a callback to be invoked when an interrupt is
// raised.
// Returns `ZX_ERR_ALREADY_EXISTS` if a callback is already set.
static inline zx_status_t ihda_dsp_irq_enable(const ihda_dsp_protocol_t* proto,
                                              const ihda_dsp_irq_t* callback) {
    return proto->ops->irq_enable(proto->ctx, callback);
}
// Disable DSP interrupts and clears the callback.
static inline void ihda_dsp_irq_disable(const ihda_dsp_protocol_t* proto) {
    proto->ops->irq_disable(proto->ctx);
}

__END_CDECLS;
