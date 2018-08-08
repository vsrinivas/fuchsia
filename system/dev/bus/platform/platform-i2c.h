// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddktl/protocol/i2c-impl.h>
#include <fbl/mutex.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformI2cBus {
public:
    explicit PlatformI2cBus(i2c_impl_protocol_t* i2c, uint32_t bus_id);
    zx_status_t Start();

    zx_status_t Transact(uint32_t txid, rpc_i2c_req_t* req, uint16_t address, const void* write_buf,
                         zx_handle_t channel_handle);
private:
    // struct representing an I2C transaction.
    struct I2cTxn {
        uint32_t txid;
        zx_handle_t channel_handle;

        list_node_t node;
        size_t write_length;
        size_t read_length;
        uint16_t address;
        i2c_complete_cb complete_cb;
        void* cookie;
        uint8_t write_buffer[];
    };

    void Complete(I2cTxn* txn, zx_status_t status, const uint8_t* data,
                  size_t data_length);
    int I2cThread();

    ddk::I2cImplProtocolProxy i2c_;
    const uint32_t bus_id_;
    size_t max_transfer_;

    list_node_t queued_txns_ __TA_GUARDED(mutex_);
    list_node_t free_txns_ __TA_GUARDED(mutex_);
    sync_completion_t txn_signal_;

    thrd_t thread_;
    fbl::Mutex mutex_;
};

} // namespace platform_bus
