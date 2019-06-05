// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <map>

class MemoryAllocator {
public:
    // Some sub-classes take this interface as a constructor param, which
    // enables a fake in tests where we don't have a real zx::bti etc.
    class Owner {
    public:
        virtual const zx::bti& bti() = 0;
        virtual zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) = 0;
    };

    virtual ~MemoryAllocator();

    virtual zx_status_t Allocate(uint64_t size, zx::vmo* vmo) = 0;
    virtual bool CoherencyDomainIsInaccessible() = 0;
    virtual zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) { return ZX_ERR_NOT_SUPPORTED; }

    void AddDestroyCallback(intptr_t key, fit::callback<void()> callback);
    void RemoveDestroyCallback(intptr_t key);

public:
    std::map<intptr_t, fit::callback<void()>> destroy_callbacks_;
};
