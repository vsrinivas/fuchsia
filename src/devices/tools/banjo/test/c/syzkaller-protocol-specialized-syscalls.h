// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.specialized.syscalls banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef union zx_handle_types zx_handle_types_t;
typedef union zx_create_types zx_create_types_t;
typedef uint32_t zx_create_options_t;
#define ZX_CREATE_OPTIONS_VARIANT0 UINT32_C(0)
#define ZX_CREATE_OPTIONS_VARIANT1 UINT32_C(1)
#define ZX_CREATE_OPTIONS_VARIANT2 UINT32_C(2)
typedef struct api_protocol api_protocol_t;

// Declarations
union zx_handle_types {
    zx_handle_t type0;
    zx_handle_t type1;
    zx_handle_t type2;
};

union zx_create_types {
    int8_t type0[1];
    int16_t type1[1];
    int32_t type2[1];
};

typedef struct api_protocol_ops {
    zx_status_t (*create)(void* ctx, zx_handle_t handle, int32_t options, const void buffer[buffer_size], size_t buffer_size);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_create(const api_protocol_t* proto, zx_handle_t handle, int32_t options, const void buffer[buffer_size], size_t buffer_size) {
    return proto->ops->create(proto->ctx, handle, options, buffer, buffer_size);
}



__END_CDECLS
