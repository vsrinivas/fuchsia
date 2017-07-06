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
static inline mx_status_t pdev_find_protocol(platform_device_protocol_t* pdev, uint32_t proto_id,
                                             void* out_proto) {
    return pdev->ops->find_protocol(pdev->ctx, proto_id, out_proto);
}

// Registers a protocol with the platform bus driver
static inline mx_status_t pdev_register_protocol(platform_device_protocol_t* pdev,
                                                 uint32_t proto_id, void* proto_ops,
                                                 void* proto_ctx) {
    return pdev->ops->register_protocol(pdev->ctx, proto_id, proto_ops, proto_ctx);
}

__END_CDECLS;
