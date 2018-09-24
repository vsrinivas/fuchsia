// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#define MAC_ARRAY_LENGTH 6

__BEGIN_CDECLS;

typedef struct {
    zx_status_t (*config_phy)(void* ctx, uint8_t* mac, uint8_t len);
    void* ctx;
} eth_mac_callbacks_t;

typedef struct {
    zx_status_t (*mdio_read)(void* ctx, uint32_t reg, uint32_t* val);
    zx_status_t (*mdio_write)(void* ctx, uint32_t reg, uint32_t val);
    zx_status_t (*register_callbacks)(void* ctx, eth_mac_callbacks_t* callbacks);
} eth_mac_protocol_ops_t;

typedef struct {
    eth_mac_protocol_ops_t* ops;
    void* ctx;
} eth_mac_protocol_t;

static inline zx_status_t mdio_read(const eth_mac_protocol_t* eth_mac,
                                    uint32_t reg,
                                    uint32_t* val) {
    return eth_mac->ops->mdio_read(eth_mac->ctx, reg, val);
}

static inline zx_status_t mdio_write(const eth_mac_protocol_t* eth_mac,
                                     uint32_t reg,
                                     uint32_t val) {
    return eth_mac->ops->mdio_write(eth_mac->ctx, reg, val);
}

static inline zx_status_t register_callbacks(const eth_mac_protocol_t* eth_mac,
                                             eth_mac_callbacks_t* callbacks) {
    return eth_mac->ops->register_callbacks(eth_mac->ctx, callbacks);
}
__END_CDECLS;
