// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_HANDLE_H
#define PLATFORM_HANDLE_H

#include "magma_util/macros.h"
#include <memory>

namespace magma {

class PlatformHandle {
public:
    PlatformHandle() = default;
    virtual ~PlatformHandle() = default;

    virtual bool GetCount(uint32_t* count_out) = 0;
    virtual uint32_t release() = 0;

    static std::unique_ptr<PlatformHandle> Create(uint32_t handle);

private:
    DISALLOW_COPY_AND_ASSIGN(PlatformHandle);
};

} // namespace magma

#endif // PLATFORM_HANDLE_H
