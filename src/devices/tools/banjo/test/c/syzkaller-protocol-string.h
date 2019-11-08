// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.string banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct api_protocol api_protocol_t;

// Declarations
typedef struct api_protocol_ops {
    zx_status_t (*string)(void* ctx, const char* str, size_t str_len);
    zx_status_t (*string)(void* ctx, const char* str, size_t str_len);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_string(const api_protocol_t* proto, const char* str, size_t str_len) {
    return proto->ops->string(proto->ctx, str, str_len);
}

static inline zx_status_t api_string(const api_protocol_t* proto, const char* str, size_t str_len) {
    return proto->ops->string(proto->ctx, str, str_len);
}



__END_CDECLS
