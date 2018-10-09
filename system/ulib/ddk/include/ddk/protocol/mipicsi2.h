// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi2 INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct mipi_info mipi_info_t;
typedef struct mipi_csi2protocol mipi_csi2protocol_t;

// Declarations

struct mipi_info {
    uint32_t channel;
    uint32_t lanes;
    uint32_t ui_value;
    uint32_t csi_version;
};

typedef struct mipi_csi2protocol_ops {
    zx_status_t (*init)(void* ctx, const mipi_info_t* info);
    zx_status_t (*de_init)(void* ctx);
} mipi_csi2protocol_ops_t;

struct mipi_csi2protocol {
    mipi_csi2protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t mipi_csi2_init(const mipi_csi2protocol_t* proto,
                                         const mipi_info_t* info) {
    return proto->ops->init(proto->ctx, info);
}
static inline zx_status_t mipi_csi2_de_init(const mipi_csi2protocol_t* proto) {
    return proto->ops->de_init(proto->ctx);
}

__END_CDECLS;
