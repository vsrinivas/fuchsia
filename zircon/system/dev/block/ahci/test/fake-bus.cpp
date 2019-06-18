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

zx_status_t FakeBus::BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset, uint64_t size,
                            zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt_out) {
    return ZX_ERR_IO_NOT_PRESENT;
}


zx_status_t FakeBus::RegRead(const volatile uint32_t* reg, uint32_t* val_out) {
    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t FakeBus::RegWrite(volatile uint32_t* reg, uint32_t val) {
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
