// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <sync/completion.h>

#include <string.h>

__BEGIN_CDECLS;

typedef struct {
    zx_status_t (*get_bti)(void* ctx, uint32_t iommu_index, uint32_t bti_id,
                           zx_handle_t* out_handle);
} iommu_protocol_ops_t;

typedef struct {
    iommu_protocol_ops_t* ops;
    void* ctx;
} iommu_protocol_t;

// Returns a bus transaction initiator handle for the given index
static inline zx_status_t iommu_get_bti(iommu_protocol_t* iommu, uint32_t iommu_index,
                                        uint32_t bti_id, zx_handle_t* out_handle) {
    return iommu->ops->get_bti(iommu->ctx, iommu_index, bti_id, out_handle);
}

__END_CDECLS;
