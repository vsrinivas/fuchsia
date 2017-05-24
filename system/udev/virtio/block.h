// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "device.h"
#include "ring.h"

#include <magenta/compiler.h>
#include <stdlib.h>

namespace virtio {

class Ring;

class BlockDevice : public Device {
public:
    BlockDevice(mx_device_t* device);
    virtual ~BlockDevice();

    virtual mx_status_t Init();

    virtual void IrqRingUpdate();
    virtual void IrqConfigChange();

    uint64_t GetSize() const { return config_.capacity * config_.blk_size; }
    uint32_t GetBlockSize() const { return config_.blk_size; }
    uint64_t GetBlockCount() const { return config_.capacity; }

private:
    // DDK driver hooks
    static void virtio_block_iotxn_queue(void* ctx, iotxn_t* txn);
    static mx_off_t virtio_block_get_size(void* ctx);
    static mx_status_t virtio_block_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual);

    void QueueReadWriteTxn(iotxn_t* txn);

    // the main virtio ring
    Ring vring_ = {this};

    // saved block device configuration out of the pci config BAR
    struct virtio_blk_config {
        uint64_t capacity;
        uint32_t size_max;
        uint32_t seg_max;
        struct virtio_blk_geometry {
            uint16_t cylinders;
            uint8_t heads;
            uint8_t sectors;
        } geometry;
        uint32_t blk_size;
    } config_ __PACKED = {};

    struct virtio_blk_req {
        uint32_t type;
        uint32_t ioprio;
        uint64_t sector;
    } __PACKED;

    // a queue of block request/responses
    static const size_t blk_req_count = 32;

    mx_paddr_t blk_req_pa_ = 0;
    virtio_blk_req* blk_req_ = nullptr;

    mx_paddr_t blk_res_pa_ = 0;
    uint8_t* blk_res_ = nullptr;

    uint32_t blk_req_bitmap_ = 0;

    unsigned int alloc_blk_req() {
        unsigned int i = 31 - __builtin_clz(blk_req_bitmap_);
        blk_req_bitmap_ |= (1 << i);
        return i;
    }

    void free_blk_req(unsigned int i) {
        blk_req_bitmap_ &= ~(1 << i);
    }

    // pending iotxns
    list_node iotxn_list = LIST_INITIAL_VALUE(iotxn_list);
};

} // namespace virtio
