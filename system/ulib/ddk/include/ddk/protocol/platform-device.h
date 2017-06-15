// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

typedef struct {
    mx_status_t (*find_protocol)(void* ctx, uint32_t proto_id, void* out);
    mx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, void* proto_ops, void* proto_ctx);
} platform_device_protocol_ops_t;

typedef struct {
    platform_device_protocol_ops_t* ops;
    void* ctx;
} platform_device_protocol_t;

// Looks for a platform device that implements a given protocol
mx_status_t platform_device_find_protocol(mx_device_t* dev, uint32_t proto_id, void* out);

__END_CDECLS;
