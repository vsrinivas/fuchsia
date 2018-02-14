// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct clk_protocol_ops {
    zx_status_t (*enable)(void* ctx, uint32_t index);
    zx_status_t (*disable)(void* ctx, uint32_t index);
} clk_protocol_ops_t;

typedef struct {
    clk_protocol_ops_t* ops;
    void* ctx;
} clk_protocol_t;

static inline zx_status_t clk_enable(clk_protocol_t * clk, const uint32_t index) {
    return clk->ops->enable(clk->ctx, index);
}

static inline zx_status_t clk_disable(clk_protocol_t * clk, const uint32_t index) {
    return clk->ops->disable(clk->ctx, index);
}

__END_CDECLS;