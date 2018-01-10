// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/vmar_manager.h>

namespace fbl {

RefPtr<VmarManager> VmarManager::Create(size_t size,
                                        RefPtr<VmarManager> parent,
                                        uint32_t flags) {
    if (!size || (parent && !parent->vmar().is_valid())) {
        return nullptr;
    }

    AllocChecker ac;
    RefPtr<VmarManager> ret = fbl::AdoptRef(new (&ac) VmarManager());

    if (!ac.check()) {
        return nullptr;
    }

    zx_status_t res;
    zx_handle_t p = parent ? parent->vmar().get() : zx::vmar::root_self().get();
    uintptr_t child_addr;

    res = zx_vmar_allocate(p, 0, size, flags, ret->vmar_.reset_and_get_address(), &child_addr);
    if (res != ZX_OK) {
        return nullptr;
    }

    ret->parent_ = fbl::move(parent);
    ret->start_ = reinterpret_cast<void*>(child_addr);
    ret->size_ = size;

    return ret;
}

} // namespace fbl
