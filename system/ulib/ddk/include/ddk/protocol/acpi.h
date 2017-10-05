// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct acpi_protocol_ops {
    zx_status_t (*map_resource)(void* ctx, uint32_t res_id, uint32_t cache_policy,
                                void** vaddr, size_t* size, zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, int which_irq, zx_handle_t* out_handle);
} acpi_protocol_ops_t;

typedef struct acpi_protocol {
    acpi_protocol_ops_t* ops;
    void* ctx;
} acpi_protocol_t;

static inline zx_status_t acpi_map_resource(acpi_protocol_t* acpi, uint32_t res_id,
                                           uint32_t cache_policy, void** vaddr, size_t* size,
                                           zx_handle_t* out_handle) {
    return acpi->ops->map_resource(acpi->ctx, res_id, cache_policy, vaddr, size, out_handle);
}

static inline zx_status_t acpi_map_interrupt(acpi_protocol_t* acpi, int which_irq,
                                            zx_handle_t* out_handle) {
    return acpi->ops->map_interrupt(acpi->ctx, which_irq, out_handle);
}

__END_CDECLS;
