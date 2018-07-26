// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/usb.h>
#include <lib/sync/completion.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>

#include <threads.h>

// struct representing a block device for a logical unit
typedef struct {
    zx_device_t* zxdev;         // block device we publish

    uint64_t total_blocks;
    uint32_t block_size;

    uint8_t lun;                // our logical unit number
    uint32_t flags;             // flags for block_info_t
    bool device_added;
} ums_block_t;

// main struct for the UMS driver
typedef struct {
    zx_device_t* zxdev;         // root device we publish
    zx_device_t* usb_zxdev;     // USB device we are bound to
    usb_protocol_t usb;

    uint32_t tag_send;          // next tag to send in CBW
    uint32_t tag_receive;       // next tag we expect to receive in CSW

    uint8_t max_lun;            // index of last logical unit
    size_t max_transfer;        // maximum transfer size reported by usb_get_max_transfer_size()

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    size_t bulk_in_max_packet;
    size_t bulk_out_max_packet;

    usb_request_t* cbw_req;
    usb_request_t* data_req;
    usb_request_t* csw_req;

    usb_request_t data_transfer_req;  // for use in ums_data_transfer

    thrd_t worker_thread;
    bool dead;

    // list of queued transactions
    list_node_t queued_txns;

    sync_completion_t txn_completion;    // signals ums_worker_thread when new txns are available
                                    // and when device is dead
    mtx_t txn_lock;                 // protects queued_txns, txn_completion and dead

    ums_block_t block_devs[];
} ums_t;
#define block_to_ums(block) containerof(block - block->lun, ums_t, block_devs)

typedef struct ums_txn {
    block_op_t op;
    list_node_t node;
    ums_block_t* dev;
} ums_txn_t;
#define block_op_to_txn(op) containerof(op, ums_txn_t, op)

zx_status_t ums_block_add_device(ums_t* ums, ums_block_t* dev);
