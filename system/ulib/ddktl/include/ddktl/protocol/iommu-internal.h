// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_get_bti, GetBti,
        zx_status_t (C::*)(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle));

template <typename D>
constexpr void CheckIommuProtocolSubclass() {
    static_assert(internal::has_get_bti<D>::value,
                  "IommuProtocol subclasses must implement "
                  "GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle)");
 }

}  // namespace internal
}  // namespace ddk
