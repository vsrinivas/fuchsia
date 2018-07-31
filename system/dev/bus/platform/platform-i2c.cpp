// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-i2c.h"

#include <stdlib.h>
#include <threads.h>
#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <zircon/listnode.h>
#include <zircon/threads.h>

PlatformI2cBus::PlatformI2cBus(i2c_impl_protocol_t* i2c, uint32_t bus_id)
    : i2c_(i2c), bus_id_(bus_id) {

    list_initialize(&queued_txns_);
    list_initialize(&free_txns_);
    sync_completion_reset(&txn_signal_);
}

zx_status_t PlatformI2cBus::Start() {
    auto status = i2c_.GetMaxTransferSize(bus_id_, &max_transfer_);
    if (status != ZX_OK) {
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "PlatformI2cBus[%u]", bus_id_);
    auto thunk = [](void* arg) -> int { return static_cast<PlatformI2cBus*>(arg)->I2cThread(); };
    thrd_create_with_name(&thread_, thunk, this, name);

    return ZX_OK;
}

void PlatformI2cBus::Complete(I2cTxn* txn, zx_status_t status, const uint8_t* data,
                                 size_t data_length) {
    struct {
        pdev_resp_t resp;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE] = {};
    } resp = {
        .resp = {
            .txid = txn->txid,
            .status = status,
            .i2c_txn = {
                .write_length = 0,
                .read_length = 0,
                .complete_cb = txn->complete_cb,
                .cookie = txn->cookie,
            },
        },
    };

    if (status == ZX_OK) {
        memcpy(resp.data, data, data_length);
    }

    status = zx_channel_write(txn->channel_handle, 0, &resp,
                              static_cast<uint32_t>(sizeof(resp.resp) + data_length), nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_i2c_read_complete: zx_channel_write failed %d\n", status);
    }
}

int PlatformI2cBus::I2cThread() {
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t> read_buffer(new (&ac) uint8_t[max_transfer_]);
    if (!ac.check()) {
        zxlogf(ERROR, "%s could not allocate read_buffer\n", __FUNCTION__);
        return 0;
    }

    while (1) {
        sync_completion_wait(&txn_signal_, ZX_TIME_INFINITE);
        sync_completion_reset(&txn_signal_);

        I2cTxn* txn;

        mutex_.Acquire();
        while ((txn = list_remove_head_type(&queued_txns_, I2cTxn, node)) != nullptr) {
            mutex_.Release();

            auto status = i2c_.Transact(bus_id_, txn->address,  txn->write_buffer,
                                        txn->write_length, read_buffer.get(), txn->read_length);
            size_t actual = (status == ZX_OK ? txn->read_length : 0);
            Complete(txn, status, read_buffer.get(), actual);

            mutex_.Acquire();
            list_add_tail(&free_txns_, &txn->node);
        }
        mutex_.Release();
    }
    return 0;
}

 zx_status_t PlatformI2cBus::Transact(pdev_req_t* req, uint16_t address, const void* write_buf,
                                      zx_handle_t channel_handle) {
    const size_t write_length = req->i2c_txn.write_length;
    const size_t read_length = req->i2c_txn.read_length;
    if (write_length > max_transfer_ || read_length > max_transfer_) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&mutex_);

    I2cTxn* txn = list_remove_head_type(&free_txns_, I2cTxn, node);
    if (!txn) {
        // add space for write buffer
        txn = static_cast<I2cTxn*>(calloc(1, sizeof(I2cTxn) + max_transfer_));
    }
    if (!txn) {
        return ZX_ERR_NO_MEMORY;
    }

    txn->address = address;
    txn->write_length = write_length;
    txn->read_length = read_length;
    memcpy(txn->write_buffer, write_buf, write_length);
    txn->complete_cb = req->i2c_txn.complete_cb;
    txn->cookie = req->i2c_txn.cookie;
    txn->txid = req->txid;
    txn->channel_handle = channel_handle;

    list_add_tail(&queued_txns_, &txn->node);
    sync_completion_signal(&txn_signal_);

    return ZX_OK;
}
