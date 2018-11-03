// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <string.h>

namespace fzl {

zx_status_t OwnedVmoMapper::CreateAndMap(uint64_t size,
                                         const char* name,
                                         zx_vm_option_t map_options,
                                         fbl::RefPtr<VmarManager> vmar_manager,
                                         uint32_t cache_policy) {
    zx::vmo temp;
    zx_status_t res = VmoMapper::CreateAndMap(size,
                                              map_options,
                                              fbl::move(vmar_manager),
                                              &temp,
                                              ZX_RIGHT_SAME_RIGHTS,
                                              cache_policy);

    if (res == ZX_OK) {
        temp.set_property(ZX_PROP_NAME, name, name ? strlen(name) : 0);
        vmo_ = fbl::move(temp);
    }

    return res;
}

zx_status_t OwnedVmoMapper::Map(zx::vmo vmo,
                                uint64_t size,
                                zx_vm_option_t map_options,
                                fbl::RefPtr<VmarManager> vmar_manager) {
    zx_status_t res = VmoMapper::Map(vmo, 0, size, map_options, vmar_manager);

    if (res == ZX_OK) {
        vmo_ = fbl::move(vmo);
    }

    return res;
}

} // namespace fzl
