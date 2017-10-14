// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

typedef struct zx_port_packet zx_port_packet_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_ctx vcpu_ctx_t;
typedef zx_status_t (*read_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef zx_status_t (*write_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Stores the state associated with a single VCPU. */
typedef struct vcpu_ctx {
    vcpu_ctx(zx_handle_t vcpu_);

    zx_handle_t vcpu;
    read_state_fn_t read_state;
    write_state_fn_t write_state;
} vcpu_ctx_t;

/* Controls execution of a VCPU context, providing the main logic. */
zx_status_t vcpu_loop(vcpu_ctx_t* vcpu_ctx);

/* Processes a single guest packet. */
zx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, zx_port_packet_t* packet);
