// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/vmo.h>

class ProtectedMemoryAllocator {
public:
    virtual ~ProtectedMemoryAllocator() {}

    virtual zx_status_t Allocate(uint64_t size, zx::vmo* vmo) = 0;
    virtual zx_status_t GetProtectedMemoryInfo(uint64_t* base, uint64_t* size) = 0;
};
