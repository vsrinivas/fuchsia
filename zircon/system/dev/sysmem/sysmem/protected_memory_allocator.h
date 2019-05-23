// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/vmo.h>

#include "memory_allocator.h"

class ProtectedMemoryAllocator : public MemoryAllocator {
public:
    virtual ~ProtectedMemoryAllocator() {}

    virtual zx_status_t GetProtectedMemoryInfo(uint64_t* base, uint64_t* size) = 0;
};
