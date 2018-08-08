// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_INTERRUPT_H
#define ZIRCON_PLATFORM_INTERRUPT_H

#include "platform_interrupt.h"

#include <utility>

#include <ddk/device.h>
#include <ddk/protocol/pci.h>
#include <lib/zx/handle.h>

namespace magma {

class ZirconPlatformInterrupt : public PlatformInterrupt {
public:
    ZirconPlatformInterrupt(zx::handle interrupt_handle) : handle_(std::move(interrupt_handle))
    {
        DASSERT(handle_.get() != ZX_HANDLE_INVALID);
    }

    void Signal() override { zx_interrupt_destroy(handle_.get()); }

    bool Wait() override
    {
        zx_status_t status = zx_interrupt_wait(handle_.get(), nullptr);
        if (status != ZX_OK)
            return DRETF(false, "zx_irq_wait failed (%d)", status);
        return true;
    }

    void Complete() override {}

private:
    zx::handle handle_;
};

} // namespace magma

#endif // ZIRCON_PLATFORM_INTERRUPT_H
