// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "../bus.h"

namespace ahci {

// Fake bus for unit testing the AHCI driver.

class FakeBus : public Bus {
public:
    virtual ~FakeBus() override;
    virtual zx_status_t Configure(zx_device_t* parent) override;
    virtual zx_status_t IoBufferInit(io_buffer_t* buffer_, size_t size, uint32_t flags,
                                     zx_paddr_t* phys_out, void** virt_out) override;
    virtual zx_status_t BtiPin(uint32_t options, const zx::unowned_vmo& vmo, uint64_t offset, uint64_t size,
                               zx_paddr_t* addrs, size_t addrs_count, zx::pmt* pmt_out) override;

    virtual zx_status_t RegRead(const volatile uint32_t* reg, uint32_t* val_out) override;
    virtual zx_status_t RegWrite(volatile uint32_t* reg, uint32_t val) override;

    virtual zx_status_t InterruptWait() override;

    virtual void* mmio() override { return nullptr; }



    // Test control functions.

    // Cause calls to Configure() to return an error.
    void DoFailConfigure() { fail_configure_ = true; }

private:
    bool fail_configure_ = false;
};

} // namespace ahci
