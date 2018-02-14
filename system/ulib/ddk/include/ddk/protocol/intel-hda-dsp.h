// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef void (ihda_dsp_irq_callback_t)(void* cookie);

typedef struct ihda_dsp_protocol_ops {
    // Fetch the parent HDA controller's PCI device info
    void (*get_dev_info)(void* ctx, zx_pcie_device_info_t* out_info);

    // Fetch a VMO that represents the BAR holding the Audio DSP registers.
    zx_status_t (*get_mmio)(void* ctx, zx_handle_t* out_vmo, size_t* out_size);

    // Fetch a handle to our bus transaction initiator.
    zx_status_t (*get_bti)(void* ctx, zx_handle_t* out_handle);

    // Enables DSP
    void (*enable)(void* ctx);

    // Disable DSP
    void (*disable)(void* ctx);

    // Enables DSP interrupts and set a callback to be invoked when an interrupt is
    // raised.
    // Returns ZX_ERR_ALREADY_EXISTS if a callback is already set.
    zx_status_t (*irq_enable)(void* ctx, ihda_dsp_irq_callback_t* callback, void* cookie);

    // Disable DSP interrupts and clears the callback.
    void (*irq_disable)(void* ctx);
} ihda_dsp_protocol_ops_t;

typedef struct ihda_dsp_protocol {
    ihda_dsp_protocol_ops_t* ops;
    void* ctx;
} ihda_dsp_protocol_t;

static inline void ihda_dsp_get_dev_info(const ihda_dsp_protocol_t* ihda_dsp,
                                         zx_pcie_device_info_t* out_info) {
    ihda_dsp->ops->get_dev_info(ihda_dsp->ctx, out_info);
}

static inline zx_status_t ihda_dsp_get_mmio(const ihda_dsp_protocol_t* ihda_dsp,
                                            zx_handle_t* out_vmo,
                                            size_t* out_size) {
    return ihda_dsp->ops->get_mmio(ihda_dsp->ctx, out_vmo, out_size);
}

static inline zx_status_t ihda_dsp_get_bti(const ihda_dsp_protocol_t* ihda_dsp,
                                           zx_handle_t* out_handle) {
    return ihda_dsp->ops->get_bti(ihda_dsp->ctx, out_handle);
}

static inline void ihda_dsp_enable(const ihda_dsp_protocol_t* ihda_dsp) {
    return ihda_dsp->ops->enable(ihda_dsp->ctx);
}

static inline void ihda_dsp_disable(const ihda_dsp_protocol_t* ihda_dsp) {
    return ihda_dsp->ops->disable(ihda_dsp->ctx);
}

static inline zx_status_t ihda_dsp_irq_enable(const ihda_dsp_protocol_t* ihda_dsp,
                                              ihda_dsp_irq_callback_t* callback,
                                              void* cookie) {
    return ihda_dsp->ops->irq_enable(ihda_dsp->ctx, callback, cookie);
}

static inline void ihda_dsp_irq_disable(const ihda_dsp_protocol_t* ihda_dsp) {
    ihda_dsp->ops->irq_disable(ihda_dsp->ctx);
}

__END_CDECLS;
