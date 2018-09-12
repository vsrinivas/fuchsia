// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/iommu.banjo INSTEAD.

#pragma once

#include <ddk/protocol/iommu.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_iommu_protocol_get_bti, IommuGetBti,
                                     zx_status_t (C::*)(uint32_t iommu_index, uint32_t bti_id,
                                                        zx_handle_t* out_handle));

template <typename D>
constexpr void CheckIommuProtocolSubclass() {
    static_assert(
        internal::has_iommu_protocol_get_bti<D>::value,
        "IommuProtocol subclasses must implement "
        "zx_status_t IommuGetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle");
}

} // namespace internal
} // namespace ddk
