// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGENTA_PLATFORM_INTERRUPT_H
#define MAGENTA_PLATFORM_INTERRUPT_H

#include "platform_interrupt.h"

#include <ddk/device.h>
#include <ddk/protocol/pci.h>

namespace magma {

class MagentaPlatformInterrupt : public PlatformInterrupt {
public:
    MagentaPlatformInterrupt(mx_handle_t interrupt_handle) : handle_(interrupt_handle)
    {
        DASSERT(handle_ != MX_HANDLE_INVALID);
    }

    ~MagentaPlatformInterrupt() override { Close(); }

    void Close() override { mx_handle_close(handle_); }

    bool Wait() override
    {
        mx_status_t status = mx_interrupt_wait(handle_);
        if (status != MX_OK)
            return DRETF(false, "mx_interrupt_wait failed (%d)", status);
        return true;
    }

    void Complete() override { mx_interrupt_complete(handle_); }

private:
    mx_handle_t handle_;
};

} // namespace magma

#endif // MAGENTA_PLATFORM_INTERRUPT_H
