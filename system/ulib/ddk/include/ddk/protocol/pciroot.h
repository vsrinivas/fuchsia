// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct pciroot_protocol pciroot_protocol_t;

// Declarations

typedef struct pciroot_protocol_ops {
    zx_status_t (*get_auxdata)(void* ctx, const char* args, void* out_data_buffer, size_t data_size,
                               size_t* out_data_actual);
    zx_status_t (*get_bti)(void* ctx, uint32_t bdf, uint32_t index, zx_handle_t* out_bti);
} pciroot_protocol_ops_t;

struct pciroot_protocol {
    pciroot_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t pciroot_get_auxdata(const pciroot_protocol_t* proto, const char* args,
                                              void* out_data_buffer, size_t data_size,
                                              size_t* out_data_actual) {
    return proto->ops->get_auxdata(proto->ctx, args, out_data_buffer, data_size, out_data_actual);
}
static inline zx_status_t pciroot_get_bti(const pciroot_protocol_t* proto, uint32_t bdf,
                                          uint32_t index, zx_handle_t* out_bti) {
    return proto->ops->get_bti(proto->ctx, bdf, index, out_bti);
}

__END_CDECLS;
