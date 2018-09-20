// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    void (*reset_phy)(void* ctx);
} eth_board_protocol_ops_t;

typedef struct {
    eth_board_protocol_ops_t* ops;
    void* ctx;
} eth_board_protocol_t;

static inline void eth_board_reset_phy(const eth_board_protocol_t* eth_board) {
    return eth_board->ops->reset_phy(eth_board->ctx);
}
__END_CDECLS;
