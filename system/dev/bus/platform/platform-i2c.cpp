// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>
#include <zircon/listnode.h>
#include <zircon/threads.h>
#include <stdlib.h>
#include <threads.h>

#include "platform-bus.h"
#include "platform-proxy.h"

typedef struct platform_i2c_bus {
    i2c_impl_protocol_t i2c;
    uint32_t bus_id;
    size_t max_transfer;

    list_node_t queued_txns;
    list_node_t free_txns;
    sync_completion_t txn_signal;

    thrd_t thread;
    mtx_t lock;
} platform_i2c_bus_t;

typedef struct {
    uint32_t txid;
    zx_handle_t channel_handle;


    list_node_t node;
    size_t write_length;
    size_t read_length;
    uint16_t address;
    i2c_complete_cb complete_cb;
    void* cookie;
    uint8_t write_buffer[];
} i2c_txn_t;

static void platform_i2c_complete(i2c_txn_t* txn, zx_status_t status, const uint8_t* data,
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

static int i2c_bus_thread(void *arg) {
    platform_i2c_bus_t* i2c_bus = static_cast<platform_i2c_bus_t*>(arg);

    uint8_t* read_buffer = static_cast<uint8_t*>(malloc(i2c_bus->max_transfer));
    if (!read_buffer) {
        return -1;
    }

    while (1) {
        sync_completion_wait(&i2c_bus->txn_signal, ZX_TIME_INFINITE);
        sync_completion_reset(&i2c_bus->txn_signal);

        i2c_txn_t* txn;

        mtx_lock(&i2c_bus->lock);
        while ((txn = list_remove_head_type(&i2c_bus->queued_txns, i2c_txn_t, node)) != nullptr) {
            mtx_unlock(&i2c_bus->lock);

            zx_status_t status = i2c_impl_transact(&i2c_bus->i2c, i2c_bus->bus_id, txn->address,
                                                        txn->write_buffer, txn->write_length,
                                                        read_buffer, txn->read_length);
            size_t actual = (status == ZX_OK ? txn->read_length : 0);
            platform_i2c_complete(txn, status, read_buffer, actual);

            mtx_lock(&i2c_bus->lock);
            list_add_tail(&i2c_bus->free_txns, &txn->node);
        }
        mtx_unlock(&i2c_bus->lock);
    }

    free(read_buffer);
    return 0;
}

zx_status_t platform_i2c_init(platform_bus_t* bus, i2c_impl_protocol_t* i2c) {
    if (bus->i2c_buses) {
        // already initialized
        return ZX_ERR_BAD_STATE;
    }

    uint32_t bus_count = i2c_impl_get_bus_count(i2c);
    if (!bus_count) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    platform_i2c_bus_t* i2c_buses = static_cast<platform_i2c_bus_t*>(calloc(bus_count,
                                                                     sizeof(platform_i2c_bus_t)));
    if (!i2c_buses) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = ZX_OK;

    for (uint32_t i = 0; i < bus_count; i++) {
        platform_i2c_bus_t* i2c_bus = &i2c_buses[i];

        i2c_bus->bus_id = i;
        mtx_init(&i2c_bus->lock, mtx_plain);
        list_initialize(&i2c_bus->queued_txns);
        list_initialize(&i2c_bus->free_txns);
        sync_completion_reset(&i2c_bus->txn_signal);
        memcpy(&i2c_bus->i2c, i2c, sizeof(i2c_bus->i2c));

        status = i2c_impl_get_max_transfer_size(i2c, i, &i2c_bus->max_transfer);
        if (status != ZX_OK) {
            goto fail;
        }

        char name[32];
        snprintf(name, sizeof(name), "i2c_bus_thread[%u]", i);
        thrd_create_with_name(&i2c_bus->thread, i2c_bus_thread, i2c_bus, name);
    }

    bus->i2c_buses = i2c_buses;
    bus->i2c_bus_count = bus_count;

    return ZX_OK;

fail:
    free(i2c_buses);
    return status;
}

zx_status_t platform_i2c_transact(platform_bus_t* bus, pdev_req_t* req, pbus_i2c_channel_t* channel,
                                  const void* write_buf, zx_handle_t channel_handle) {
    if (channel->bus_id >= bus->i2c_bus_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    platform_i2c_bus_t* i2c_bus = &bus->i2c_buses[channel->bus_id];

    const size_t write_length = req->i2c_txn.write_length;
    const size_t read_length = req->i2c_txn.read_length;
    if (write_length > i2c_bus->max_transfer || read_length > i2c_bus->max_transfer) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&i2c_bus->lock);

    i2c_txn_t* txn = list_remove_head_type(&i2c_bus->free_txns, i2c_txn_t, node);
    if (!txn) {
        // add space for write buffer
        txn = static_cast<i2c_txn_t*>(calloc(1, sizeof(i2c_txn_t) + i2c_bus->max_transfer));
    }
    if (!txn) {
        mtx_unlock(&i2c_bus->lock);
        return ZX_ERR_NO_MEMORY;
    }

    txn->address = channel->address;
    txn->write_length = write_length;
    txn->read_length = read_length;
    memcpy(txn->write_buffer, write_buf, write_length);
    txn->complete_cb = req->i2c_txn.complete_cb;
    txn->cookie = req->i2c_txn.cookie;
    txn->txid = req->txid;
    txn->channel_handle = channel_handle;

    list_add_tail(&i2c_bus->queued_txns, &txn->node);
    mtx_unlock(&i2c_bus->lock);
    sync_completion_signal(&i2c_bus->txn_signal);

    return ZX_OK;
}
