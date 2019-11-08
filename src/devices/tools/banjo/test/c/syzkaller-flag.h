// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.flag banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef uint32_t topic_t;
#define TOPIC_TOPIC0 UINT32_C(0)
#define TOPIC_TOPIC1 UINT32_C(1)
#define TOPIC_TOPIC2 UINT32_C(2)
#define TOPIC_TOPIC3 UINT32_C(3)
#define TOPIC_TOPIC4 UINT32_C(4)
#define TOPIC_TOPIC5 UINT32_C(5)
typedef struct api_protocol api_protocol_t;

// Declarations
typedef struct api_protocol_ops {
    zx_status_t (*topic)(void* ctx, zx_handle_t h, int32_t t);
} api_protocol_ops_t;


struct api_protocol {
    api_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t api_topic(const api_protocol_t* proto, zx_handle_t h, int32_t t) {
    return proto->ops->topic(proto->ctx, h, t);
}



__END_CDECLS
