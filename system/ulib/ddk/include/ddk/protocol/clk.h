// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/clk.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct clk_protocol clk_protocol_t;

// Declarations

typedef struct clk_protocol_ops {
    zx_status_t (*enable)(void* ctx, uint32_t index);
    zx_status_t (*disable)(void* ctx, uint32_t index);
} clk_protocol_ops_t;

struct clk_protocol {
    clk_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t clk_enable(const clk_protocol_t* proto, uint32_t index) {
    return proto->ops->enable(proto->ctx, index);
}
static inline zx_status_t clk_disable(const clk_protocol_t* proto, uint32_t index) {
    return proto->ops->disable(proto->ctx, index);
}

__END_CDECLS;
