// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-bus.h"

namespace ahci {

FakeBus::~FakeBus() {}

zx_status_t FakeBus::Configure(zx_device_t* parent) {
    if (fail_configure_) return ZX_ERR_IO;
    return ZX_OK;
}

zx_status_t FakeBus::IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                  zx_paddr_t* phys_out, void** virt_out) {
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset,
                            uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt_out)
                            {
    return ZX_ERR_IO_NOT_PRESENT;
}

// Read registers in the Host Bus Adapter.
zx_status_t FakeBus::HbaRead(size_t offset, uint32_t* val_out) {
    switch (offset) {
    case kHbaGlobalHostControl:
        *val_out = ghc_;
        return ZX_OK;
    default:
        ZX_DEBUG_ASSERT(false);
        break;
    }
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::HbaWrite(size_t offset, uint32_t val) {
    switch (offset) {
    case kHbaGlobalHostControl:
        if (val & AHCI_GHC_HR) {
            // Reset was asserted. This bit clears asynchronously when reset has succeded.
            // Clear immediately until async response is supported.
            val &= ~AHCI_GHC_HR;
        }
        ghc_ = val;
        return ZX_OK;
    default:
        ZX_DEBUG_ASSERT(false);
        break;
    }
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::RegRead(size_t offset, uint32_t* val_out) {
    if (offset < kHbaPorts) {
        return HbaRead(offset, val_out);
    }
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::RegWrite(size_t offset, uint32_t val) {
    if (offset < kHbaPorts) {
        return HbaWrite(offset, val);
    }
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::InterruptWait() {
    sync_completion_wait(&irq_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&irq_completion_);
    if (interrupt_cancelled_) return ZX_ERR_CANCELED;
    return ZX_OK;
}

void FakeBus::InterruptCancel() {
    interrupt_cancelled_ = true;
    sync_completion_signal(&irq_completion_);
}

} // namespace ahci
