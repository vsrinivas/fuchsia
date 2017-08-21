// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_INTERRUPT_H
#define MAGENTA_PLATFORM_INTERRUPT_H

#include "platform_interrupt.h"

#include "mx/handle.h"
#include <ddk/device.h>
#include <ddk/protocol/pci.h>

namespace magma {

class MagentaPlatformInterrupt : public PlatformInterrupt {
public:
    MagentaPlatformInterrupt(mx::handle interrupt_handle) : handle_(std::move(interrupt_handle))
    {
        DASSERT(handle_.get() != MX_HANDLE_INVALID);
    }

    void Signal() override { mx_interrupt_signal(handle_.get()); }

    bool Wait() override
    {
        mx_status_t status = mx_interrupt_wait(handle_.get());
        if (status != MX_OK)
            return DRETF(false, "mx_interrupt_wait failed (%d)", status);
        return true;
    }

    void Complete() override { mx_interrupt_complete(handle_.get()); }

private:
    mx::handle handle_;
};

} // namespace magma

#endif // MAGENTA_PLATFORM_INTERRUPT_H
