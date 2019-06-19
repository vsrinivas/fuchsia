// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/sync/completion.h>

#include "../ahci.h"
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

    virtual zx_status_t RegRead(size_t offset, uint32_t* val_out) override;
    virtual zx_status_t RegWrite(size_t offset, uint32_t val) override;

    virtual zx_status_t InterruptWait() override;
    virtual void InterruptCancel() override;

    virtual void* mmio() override { return nullptr; }


    // Test control functions.

    // Cause calls to Configure() to return an error.
    void DoFailConfigure() { fail_configure_ = true; }

private:
    zx_status_t HbaRead(size_t offset, uint32_t* val_out);
    zx_status_t HbaWrite(size_t offset, uint32_t val);

    sync_completion_t irq_completion_;
    bool interrupt_cancelled_ = false;

    bool fail_configure_ = false;

    // Fake host bus adapter registers.
    uint32_t ghc_ = 0;

};

} // namespace ahci
