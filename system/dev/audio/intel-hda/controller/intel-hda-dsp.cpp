// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <zircon/assert.h>

#include "intel-hda-controller.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

#define DEV  static_cast<IntelHDAController*>(ctx)
zx_protocol_device_t IntelHDAController::DSP_DEVICE_THUNKS = {
    .version      = DEVICE_OPS_VERSION,
    .get_protocol = nullptr,
    .open         = nullptr,
    .open_at      = nullptr,
    .close        = nullptr,
    .unbind       = nullptr,
    .release      = nullptr,
    .read         = nullptr,
    .write        = nullptr,
    .get_size     = nullptr,
    .ioctl        = nullptr,
    .suspend      = nullptr,
    .resume       = nullptr,
    .rxrpc        = nullptr,
};

ihda_dsp_protocol_ops_t IntelHDAController::DSP_PROTO_THUNKS = {
    .get_dev_info = [](void* ctx, zx_pcie_device_info_t* out_info)
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->DspGetDevInfo(out_info);
    },
    .get_mmio = [](void* ctx, zx_handle_t* out_vmo, size_t* out_size) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->DspGetMmio(out_vmo, out_size);
    },
    .get_bti = [](void* ctx, zx_handle_t* out_handle) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->DspGetBti(out_handle);
    },
    .enable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        DEV->DspEnable();
    },
    .disable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        DEV->DspDisable();
    },
    .irq_enable = [](void* ctx, ihda_dsp_irq_callback_t* callback, void* cookie) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->DspIrqEnable(callback, cookie);
    },
    .irq_disable = [](void* ctx)
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->DspIrqDisable();
    }
};
#undef DEV

void IntelHDAController::DspGetDevInfo(zx_pcie_device_info_t* out_info) {
    if (!out_info) {
        return;
    }
    memcpy(out_info, &pci_dev_info_, sizeof(*out_info));
}

zx_status_t IntelHDAController::DspGetMmio(zx_handle_t* out_vmo, size_t* out_size) {
    // Fetch the BAR which the Audio DSP registers (BAR 4), then sanity check the type
    // and size.
    zx_pci_bar_t bar_info;
    zx_status_t res = pci_get_bar(&pci_, 4u, &bar_info);
    if (res != ZX_OK) {
        LOG(ERROR, "Error attempting to fetch registers from PCI (res %d)\n", res);
        return res;
    }

    if (bar_info.type != PCI_BAR_TYPE_MMIO) {
        LOG(ERROR, "Bad register window type (expected %u got %u)\n",
                PCI_BAR_TYPE_MMIO, bar_info.type);
        return ZX_ERR_INTERNAL;
    }

    *out_vmo = bar_info.handle;
    *out_size = bar_info.size;
    return ZX_OK;
}

zx_status_t IntelHDAController::DspGetBti(zx_handle_t* out_handle) {
    ZX_DEBUG_ASSERT(pci_bti_ != nullptr);
    zx::bti bti;
    zx_status_t res = pci_bti_->initiator().duplicate(ZX_RIGHT_SAME_RIGHTS, &bti);
    if (res != ZX_OK) {
        LOG(ERROR, "Error duplicating BTI for DSP (res %d)\n", res);
        return res;
    }
    *out_handle = bti.release();
    return ZX_OK;
}

void IntelHDAController::DspEnable() {
    ZX_DEBUG_ASSERT(pp_regs() != nullptr);
    // Note: The GPROCEN bit does not really enable or disable the Audio DSP
    // operation, but mainly to work around some legacy Intel HD Audio driver
    // software such that if GPROCEN = 0, ADSPxBA (BAR2) is mapped to the Intel
    // HD Audio memory mapped configuration registers, for compliancy with some
    // legacy SW implementation. If GPROCEN = 1, only then ADSPxBA (BAR2) is
    // mapped to the actual Audio DSP memory mapped configuration registers.
    REG_SET_BITS<uint32_t>(&pp_regs()->ppctl, HDA_PPCTL_GPROCEN | HDA_PPCTL_PIE);
}

void IntelHDAController::DspDisable() {
    ZX_DEBUG_ASSERT(pp_regs() != nullptr);
    REG_WR(&pp_regs()->ppctl, 0u);
}

zx_status_t IntelHDAController::DspIrqEnable(ihda_dsp_irq_callback_t* callback, void* cookie) {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    if (dsp_irq_callback_ != nullptr) {
        return ZX_ERR_ALREADY_EXISTS;
    }
    ZX_DEBUG_ASSERT(dsp_irq_cookie_ == nullptr);

    dsp_irq_callback_ = callback;
    dsp_irq_cookie_ = cookie;

    return ZX_OK;
}

void IntelHDAController::DspIrqDisable() {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    REG_CLR_BITS<uint32_t>(&pp_regs()->ppctl, HDA_PPCTL_PIE);
    dsp_irq_callback_ = nullptr;
    dsp_irq_cookie_ = nullptr;
}

void IntelHDAController::ProcessDspIRQ() {
    fbl::AutoLock dsp_lock(&dsp_lock_);
    if (pp_regs() == nullptr) {
        return;
    }
    if (dsp_irq_callback_ == nullptr) {
        return;
    }
    ZX_DEBUG_ASSERT(dsp_irq_cookie_ != nullptr);
    uint32_t ppsts = REG_RD(&pp_regs()->ppsts);
    if (!(ppsts & HDA_PPSTS_PIS)) {
        return;
    }
    dsp_irq_callback_(dsp_irq_cookie_);
}

}  // namespace intel_hda
}  // namespace audio
