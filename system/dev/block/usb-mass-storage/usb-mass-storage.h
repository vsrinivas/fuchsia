// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/block.h>
#include <magenta/listnode.h>
#include <sync/completion.h>

#include <threads.h>

// stack allocated struct used to implement IOCTL_DEVICE_SYNC
typedef struct {
    iotxn_t* iotxn;             // iotxn we are waiting to complete
    completion_t completion;    // completion for IOCTL_DEVICE_SYNC to wait on
    list_node_t node;           // node for ums_t.sync_nodes list
} ums_sync_node_t;

// struct representing a block device for a logical unit
typedef struct {
    mx_device_t* mxdev;         // block device we publish
    block_callbacks_t* cb;      // callbacks for async block protocol

    uint64_t total_blocks;
    uint32_t block_size;

    uint8_t lun;                // our logical unit number
    uint32_t flags;             // flags for block_info_t
    bool device_added;
} ums_block_t;

// main struct for the UMS driver
typedef struct {
    mx_device_t* mxdev;         // root device we publish
    mx_device_t* usb_mxdev;     // USB device we are bound to

    uint32_t tag_send;          // next tag to send in CBW
    uint32_t tag_receive;       // next tag we expect to receive in CSW

    uint8_t max_lun;            // index of last logical unit
    size_t max_transfer;        // maximum transfer size reported by usb_get_max_transfer_size()

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    size_t bulk_in_max_packet;
    size_t bulk_out_max_packet;

    iotxn_t* cbw_iotxn;
    iotxn_t* data_iotxn;
    iotxn_t* csw_iotxn;

    thrd_t worker_thread;
    bool dead;

    // list of queued io transactions
    list_node_t queued_iotxns;

    completion_t iotxn_completion;  // signals ums_worker_thread when new iotxns are available
                                    // and when device is dead
    mtx_t iotxn_lock;               // protects queued_iotxns, iotxn_completion and dead

    list_node_t sync_nodes;         // list of active ums_sync_node_t
    iotxn_t* curr_txn;              // current iotxn being processed (needed for IOCTL_DEVICE_SYNC)

    ums_block_t block_devs[];
} ums_t;
#define block_to_ums(block) containerof(block - block->lun, ums_t, block_devs)

mx_status_t ums_block_add_device(ums_t* ums, ums_block_t* dev);
