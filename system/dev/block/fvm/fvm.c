// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>

#include <limits.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sync/completion.h>
#include <threads.h>

#include "fvm-private.h"

static void fvm_read_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

void iotxn_synchronous_op(zx_device_t* dev, iotxn_t* txn) {
    completion_t completion = COMPLETION_INIT;
    txn->complete_cb = fvm_read_sync_complete;
    txn->cookie = &completion;

    iotxn_queue(dev, txn);
    completion_wait(&completion, ZX_TIME_INFINITE);
}

static zx_status_t fvm_bind_c(void* ctx, zx_device_t* dev, void** cookie) {
    return fvm_bind(dev);
}

static zx_driver_ops_t fvm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fvm_bind_c,
};

ZIRCON_DRIVER_BEGIN(fvm, fvm_driver_ops, "zircon", "0.1", 2)
BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
    ZIRCON_DRIVER_END(fvm)
