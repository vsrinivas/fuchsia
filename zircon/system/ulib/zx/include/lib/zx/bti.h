// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_BTI_H_
#define LIB_ZX_BTI_H_

#include <lib/zx/handle.h>
#include <lib/zx/iommu.h>
#include <lib/zx/object.h>
#include <lib/zx/pmt.h>
#include <lib/zx/vmo.h>

namespace zx {

class bti : public object<bti> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_BTI;

    constexpr bti() = default;

    explicit bti(zx_handle_t value) : object(value) {}

    explicit bti(handle&& h) : object(h.release()) {}

    bti(bti&& other) : object(other.release()) {}

    bti& operator=(bti&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(const iommu& iommu, uint32_t options, uint64_t bti_id, bti* result);

    zx_status_t pin(uint32_t options, const vmo& vmo, uint64_t offset, uint64_t size,
                    zx_paddr_t* addrs, size_t addrs_count, pmt* pmt) const {
        return zx_bti_pin(get(), options, vmo.get(), offset, size, addrs, addrs_count,
                          pmt->reset_and_get_address());
    }

    zx_status_t release_quarantine() const { return zx_bti_release_quarantine(get()); }
};

using unowned_bti = unowned<bti>;

} // namespace zx

#endif  // LIB_ZX_BTI_H_
