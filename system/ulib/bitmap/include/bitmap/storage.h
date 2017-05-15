// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <magenta/process.h>
#include <magenta/types.h>
#include <mxalloc/new.h>
#include <mxtl/array.h>
#include <mxtl/macros.h>

#ifdef __Fuchsia__
#include <mx/vmo.h>
#include <magenta/syscalls.h>
#endif

namespace bitmap {

class DefaultStorage {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DefaultStorage);
    DefaultStorage() {};

    mx_status_t Allocate(size_t size) {
        AllocChecker ac;
        auto arr = new (&ac) char[size];
        if (!ac.check()) {
            return ERR_NO_MEMORY;
        }
        storage_.reset(arr, size);
        return NO_ERROR;
    }
    void* GetData() const { return storage_.get(); }
private:
    mxtl::Array<char> storage_;
};

#ifdef __Fuchsia__
class VmoStorage {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmoStorage);
    VmoStorage() :
        vmo_(MX_HANDLE_INVALID),
        mapped_addr_(0),
        size_(0) {}

    ~VmoStorage() {
        Release();
    }

    mx_status_t Allocate(size_t size) {
        Release();
        size_ = size;
        mx_status_t status;
        if ((status = mx::vmo::create(size_, 0, &vmo_)) != NO_ERROR) {
            return status;
        } else if ((status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_.get(), 0,
                                         size_, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                         &mapped_addr_)) != NO_ERROR) {
            vmo_.reset();
            return status;
        }
        return NO_ERROR;
    }

    void* GetData() const { MX_DEBUG_ASSERT(mapped_addr_ != 0); return (void*) mapped_addr_; }
    mx_handle_t GetVmo() const { MX_DEBUG_ASSERT(mapped_addr_ != 0); return vmo_.get(); }
private:
    void Release() {
        if (mapped_addr_ != 0) {
            mx_vmar_unmap(mx_vmar_root_self(), mapped_addr_, size_);
        }
    }
    mx::vmo vmo_;
    uintptr_t mapped_addr_;
    size_t size_;
};
#endif

} // namespace bitmap
