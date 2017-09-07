// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/mapped-vmo.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

mx_status_t MappedVmo::Create(size_t size, const char* name, fbl::unique_ptr<MappedVmo>* out) {
    mx_handle_t vmo;
    uintptr_t addr;
    mx_status_t status;
    if ((status = mx_vmo_create(size, 0, &vmo)) != MX_OK) {
        return status;
    } else if ((status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0,
                                     size, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                                     &addr)) != MX_OK) {
        mx_handle_close(vmo);
        return status;
    }

    mx_object_set_property(vmo, MX_PROP_NAME, name, strlen(name));

    fbl::AllocChecker ac;
    fbl::unique_ptr<MappedVmo> mvmo(new (&ac) MappedVmo(vmo, addr, size));
    if (!ac.check()) {
        mx_vmar_unmap(mx_vmar_root_self(), addr, size);
        mx_handle_close(vmo);
        return MX_ERR_NO_MEMORY;
    }

    *out = fbl::move(mvmo);
    return MX_OK;
}

mx_status_t MappedVmo::Shrink(size_t off, size_t len) {
    if (len == 0 || off + len > len_ || off > len_ || off + len < off) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mx_handle_t new_vmo;
    if (off > 0) {
        // Unmap everything before the offset
        if ((status = mx_vmar_unmap(mx_vmar_root_self(), addr_, off)) != MX_OK) {
            return status;
        }
    }
    if (off + len < len_) {
        // Unmap everything after the offset
        if ((status = mx_vmar_unmap(mx_vmar_root_self(), addr_ + off + len, len_ - (off + len))) != MX_OK) {
            return status;
        }
    }
    if ((status = mx_vmo_clone(vmo_, MX_VMO_CLONE_COPY_ON_WRITE, off, len, &new_vmo)) != MX_OK) {
        return status;
    }
    mx_handle_close(vmo_);
    vmo_ = new_vmo;
    addr_ = addr_ + off;
    len_ = len;
    return MX_OK;
}

mx_status_t MappedVmo::Grow(size_t len) {
    if (len < len_) {
        return MX_ERR_INVALID_ARGS;
    }

    len = fbl::roundup(len, static_cast<size_t>(PAGE_SIZE));
    mx_status_t status;
    uintptr_t addr;

    if ((status = mx_vmo_set_size(vmo_, len)) != MX_OK) {
        return status;
    }

    mx_info_vmar_t vmar_info;
    if ((status = mx_object_get_info(mx_vmar_root_self(), MX_INFO_VMAR, &vmar_info, sizeof(vmar_info), NULL, NULL)) != MX_OK) {
        return status;
    }

    // Try to extend mapping
    if ((status = mx_vmar_map(mx_vmar_root_self(), addr_ + len_ - vmar_info.base, vmo_, len_, len - len_, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC, &addr)) != MX_OK) {
        // If extension fails, create entirely new mapping and unmap the old one
        if ((status = mx_vmar_map(mx_vmar_root_self(), 0, vmo_, 0, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &addr)) != MX_OK) {
            return status;
        }

        if ((status = mx_vmar_unmap(mx_vmar_root_self(), addr_, len_)) != MX_OK) {
            return status;
        }

        addr_ = addr;
    }

    len_ = len;
    return MX_OK;
}

mx_handle_t MappedVmo::GetVmo(void) const {
    return vmo_;
}
void* MappedVmo::GetData(void) const {
    return (void*)addr_;
}

size_t MappedVmo::GetSize(void) const {
    return len_;
}

MappedVmo::MappedVmo(mx_handle_t vmo, uintptr_t addr, size_t len)
    : vmo_(vmo), addr_(addr), len_(len) {}

MappedVmo::~MappedVmo() {
    mx_vmar_unmap(mx_vmar_root_self(), addr_, len_);
    mx_handle_close(vmo_);
}
