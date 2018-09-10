// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/acpi.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct acpi_protocol acpi_protocol_t;

// Declarations

typedef struct acpi_protocol_ops {
    zx_status_t (*map_resource)(void* ctx, uint32_t resource_id, uint32_t cache_policy,
                                void** out_vaddr_buffer, size_t* vaddr_size,
                                zx_handle_t* out_handle);
    zx_status_t (*map_interrupt)(void* ctx, int64_t irq_id, zx_handle_t* out_handle);
} acpi_protocol_ops_t;

struct acpi_protocol {
    acpi_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t acpi_map_resource(const acpi_protocol_t* proto, uint32_t resource_id,
                                            uint32_t cache_policy, void** out_vaddr_buffer,
                                            size_t* vaddr_size, zx_handle_t* out_handle) {
    return proto->ops->map_resource(proto->ctx, resource_id, cache_policy, out_vaddr_buffer,
                                    vaddr_size, out_handle);
}
static inline zx_status_t acpi_map_interrupt(const acpi_protocol_t* proto, int64_t irq_id,
                                             zx_handle_t* out_handle) {
    return proto->ops->map_interrupt(proto->ctx, irq_id, out_handle);
}

__END_CDECLS;
