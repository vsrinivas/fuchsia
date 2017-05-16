// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/types.h>
#include <mxtl/unique_ptr.h>

class MappedVmo {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MappedVmo);
    virtual ~MappedVmo();

    static mx_status_t Create(size_t size, mxtl::unique_ptr<MappedVmo>* out);
    mx_handle_t GetVmo(void) const;
    void* GetData(void) const;
private:
    MappedVmo(mx_handle_t vmo, uintptr_t addr, size_t len);

    const mx_handle_t vmo_;
    const uintptr_t addr_;
    const size_t len_;
};
