// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/types.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

class MappedVmo {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MappedVmo);
    virtual ~MappedVmo();

    static zx_status_t Create(size_t size, const char* name, fbl::unique_ptr<MappedVmo>* out);

    // Attempts to reduce both the VMO size and VMAR mapping
    // from length |len_| to |len|.
    //
    // Attempting to shrink the mapping to a length of zero or
    // requesting a "shrink" that would increase the mapping size
    // returns an error.
    zx_status_t Shrink(size_t len);

    // Attempts to increase both VMO size and VMAR mapping:
    // From [addr_, addr_ + len_]
    // To   [addr_, addr_ + len]
    //
    // Attempting to grow the mapping to a length smaller than the
    // current size will return an error.
    //
    // On failure, the Mapping will be safe to use, but will remain at its original size.
    zx_status_t Grow(size_t len);

    // Returns a borrowed copied of the underlying |vmo_|, i.e. this
    // returns it directly, not a zx_handle_duplicate()d one.
    zx_handle_t GetVmo(void) const;
    size_t GetSize(void) const;
    void* GetData(void) const;

private:
    MappedVmo(zx_handle_t vmo, uintptr_t addr, size_t len);

    zx_handle_t vmo_;
    uintptr_t addr_;
    size_t len_;
};
