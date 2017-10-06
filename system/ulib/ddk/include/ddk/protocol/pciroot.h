// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/auxdata.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct pciroot_protocol_ops {
    zx_status_t (*get_auxdata)(void* ctx, auxdata_type_t type,
                               void* args, size_t args_len,
                               void* out_data, size_t out_len);
} pciroot_protocol_ops_t;

typedef struct pciroot_protocol {
    pciroot_protocol_ops_t* ops;
    void* ctx;
} pciroot_protocol_t;

static inline zx_status_t pciroot_get_auxdata(pciroot_protocol_t* pciroot,
                                              auxdata_type_t type,
                                              void* args, size_t args_len,
                                              void* out_data, size_t out_len) {
    return pciroot->ops->get_auxdata(pciroot->ctx, type, args, args_len, out_data, out_len);
}

__END_CDECLS;
