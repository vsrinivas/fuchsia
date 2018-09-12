// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/iommu.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct iommu_protocol iommu_protocol_t;

// Declarations

typedef struct iommu_protocol_ops {
    zx_status_t (*get_bti)(void* ctx, uint32_t iommu_index, uint32_t bti_id,
                           zx_handle_t* out_handle);
} iommu_protocol_ops_t;

struct iommu_protocol {
    iommu_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t iommu_get_bti(const iommu_protocol_t* proto, uint32_t iommu_index,
                                        uint32_t bti_id, zx_handle_t* out_handle) {
    return proto->ops->get_bti(proto->ctx, iommu_index, bti_id, out_handle);
}

__END_CDECLS;
