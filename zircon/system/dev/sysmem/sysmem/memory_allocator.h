// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <map>

class MemoryAllocator {
public:
    virtual ~MemoryAllocator();

    virtual zx_status_t Allocate(uint64_t size, zx::vmo* vmo) = 0;
    virtual bool CoherencyDomainIsInaccessible() = 0;

    void AddDestroyCallback(intptr_t key, fit::callback<void()> callback);
    void RemoveDestroyCallback(intptr_t key);

public:
    std::map<intptr_t, fit::callback<void()>> destroy_callbacks_;
};
