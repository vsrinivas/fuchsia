// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8mmevk.h"

#include <fbl/unique_ptr.h>

namespace imx8mmevk {

zx_status_t Board::Create(zx_device_t* parent) {
    zxlogf(INFO, "I.MX8M-Mini-EVK Board init\n");

    pbus_protocol_t pbus;
    auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        ERROR("could not get pbus protocol: %d\n", status);
        return status;
    }

    fbl::AllocChecker ac;
    auto board = fbl::make_unique_checked<Board>(&ac, parent, pbus);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = board->DdkAdd("imx8mmevk", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        ERROR("DdkAdd() error: %d\n", status);
        return status;
    }

    // Devhost now owns the board driver, we don't need to manage its lifetime.
    Board* bptr = board.release();

    status = bptr->StartAll();
    if (status != ZX_OK) {
        ERROR("StartAll() error: %d\n", status);
        bptr->DdkRelease();
        return status;
    }

    return ZX_OK;
}

zx_status_t Board::StartAll() {
    auto start_thread = [](void* arg) { return static_cast<Board*>(arg)->Thread(); };
    auto rc = thrd_create_with_name(&thread_, start_thread, this, "imx8mmevk-start-thread");
    if (rc != thrd_success) {
        ERROR("thrd_create_with_name() error: %d\n", rc);
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

int Board::Thread() {
    auto status = StartGpio();
    if (status != ZX_OK) {
        ERROR("could not start gpio driver: %d\n", status);
        return -1;
    }
    return ZX_OK;
}

} // namespace imx8mmevk

extern "C" zx_status_t imx8mmevk_bind(void* ctx, zx_device_t* parent) {
    return imx8mmevk::Board::Create(parent);
}
