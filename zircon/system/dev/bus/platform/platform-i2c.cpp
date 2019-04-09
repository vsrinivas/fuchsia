// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-i2c.h"

#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/protocol/i2c-lib.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/threads.h>

#include "proxy-protocol.h"

namespace platform_bus {

PlatformI2cBus::PlatformI2cBus(const i2c_impl_protocol_t* i2c, uint32_t bus_id)
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
    if (max_transfer_ > I2C_MAX_TRANSFER_SIZE) {
        max_transfer_ = I2C_MAX_TRANSFER_SIZE;
    }

    char name[32];
    snprintf(name, sizeof(name), "PlatformI2cBus[%u]", bus_id_);
    auto thunk = [](void* arg) -> int { return static_cast<PlatformI2cBus*>(arg)->I2cThread(); };
    thrd_create_with_name(&thread_, thunk, this, name);

    return ZX_OK;
}

void PlatformI2cBus::Complete(I2cTxn* txn, zx_status_t status, const uint8_t* resp_buffer,
                              size_t resp_length) {
    rpc_i2c_rsp_t* i2c = (rpc_i2c_rsp_t*)(resp_buffer);
    i2c->header.txid = txn->txid;
    i2c->header.status = status;
    i2c->max_transfer = 0;
    i2c->transact_cb = txn->transact_cb;
    i2c->cookie = txn->cookie;
    status = zx_channel_write(txn->channel_handle, 0, resp_buffer,
                              static_cast<uint32_t>(resp_length), nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_i2c_read_complete: zx_channel_write failed %d\n", status);
    }
}

int PlatformI2cBus::I2cThread() {
    fbl::AllocChecker ac;
    fbl::Array<uint8_t> read_buffer(new (&ac) uint8_t[PROXY_MAX_TRANSFER_SIZE],
                                    PROXY_MAX_TRANSFER_SIZE);
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
            auto rpc_ops = reinterpret_cast<i2c_rpc_op_t*>(txn + 1);
            auto p_writes = reinterpret_cast<uint8_t*>(rpc_ops) +
                txn->cnt * sizeof(i2c_rpc_op_t);
            uint8_t* p_reads = read_buffer.get() + sizeof(rpc_i2c_rsp_t);

            ZX_ASSERT(txn->cnt < I2C_MAX_RW_OPS);
            i2c_impl_op_t ops[I2C_MAX_RW_OPS];
            for (size_t i = 0; i < txn->cnt; ++i) {
                // Same address for all ops, since there is one address per channel.
                ops[i].address = txn->address;
                ops[i].data_size = rpc_ops[i].length;
                ops[i].is_read = rpc_ops[i].is_read;
                ops[i].stop = rpc_ops[i].stop;
                if (ops[i].is_read) {
                    ops[i].data_buffer = p_reads;
                    p_reads += ops[i].data_size;
                } else {
                    ops[i].data_buffer = p_writes;
                    p_writes += ops[i].data_size;
                }
            }
            auto status = i2c_.Transact(bus_id_, ops, txn->cnt);
            size_t actual = status == ZX_OK ? p_reads - read_buffer.get() : sizeof(rpc_i2c_rsp_t);
            Complete(txn, status, read_buffer.get(), actual);

            mutex_.Acquire();
            list_add_tail(&free_txns_, &txn->node);
        }
        mutex_.Release();
    }
    return 0;
}

zx_status_t PlatformI2cBus::Transact(uint32_t txid, rpc_i2c_req_t* req, uint16_t address,
                                     zx_handle_t channel_handle) {
    i2c_rpc_op_t* ops = reinterpret_cast<i2c_rpc_op_t*>(req + 1);

    size_t writes_length = 0;
    for (size_t i = 0; i < req->cnt; ++i) {
        if (ops[i].length == 0 || ops[i].length > max_transfer_) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (!ops[i].is_read) {
            writes_length += ops[i].length;
        }
    }
    // Add space for requests and writes data.
    size_t req_length = sizeof(I2cTxn) + req->cnt * sizeof(i2c_rpc_op_t) + writes_length;
    if (req_length >= PROXY_MAX_TRANSFER_SIZE) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&mutex_);

    I2cTxn* txn = list_remove_head_type(&free_txns_, I2cTxn, node);
    if (txn && txn->length < req_length) {
        free(txn);
        txn = nullptr;
    }

    if (!txn) {
        // add space for write buffer
        txn = static_cast<I2cTxn*>(calloc(1, req_length));
        txn->length = req_length;
    }
    if (!txn) {
        return ZX_ERR_NO_MEMORY;
    }

    txn->address = address;
    txn->txid = txid;
    txn->transact_cb = req->transact_cb;
    txn->cookie = req->cookie;
    txn->channel_handle = channel_handle;
    txn->cnt = req->cnt;

    auto rpc_ops = reinterpret_cast<i2c_rpc_op_t*>(req + 1);
    if (req->cnt && !(rpc_ops[req->cnt - 1].stop)) {
        list_add_tail(&free_txns_, &txn->node);
        return ZX_ERR_INVALID_ARGS; // no stop in last op in transaction
    }

    memcpy(txn + 1, req + 1, req->cnt * sizeof(i2c_rpc_op_t) + writes_length);

    list_add_tail(&queued_txns_, &txn->node);
    sync_completion_signal(&txn_signal_);

    return ZX_OK;
}

} // namespace platform_bus
