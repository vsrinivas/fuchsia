// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_HANDLE_H
#define ZIRCON_PLATFORM_HANDLE_H

#include "platform_handle.h"
#include <zx/handle.h>

namespace magma {

class ZirconPlatformHandle : public PlatformHandle {
public:
    ZirconPlatformHandle(zx::handle handle) : handle_(std::move(handle))
    {
        DASSERT(handle_ != ZX_HANDLE_INVALID);
    }

    bool GetCount(uint32_t* count_out) override;

    uint32_t release() override { return handle_.release(); }

    zx_handle_t get() { return handle_.get(); }

private:
    zx::handle handle_;
    static_assert(sizeof(handle_) == sizeof(uint32_t), "zx handle is not 32 bits");
};

} // namespace magma

#endif // ZIRCON_PLATFORM_HANDLE_H
