// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "device.h"
#include "ring.h"

#include <magenta/compiler.h>
#include <stdlib.h>

#include <ddk/protocol/block.h>
#include <virtio/block.h>

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

    static void virtio_block_set_callbacks(void* ctx, block_callbacks_t* cb);
    static void virtio_block_get_info(void* ctx, block_info_t* info);
    static void virtio_block_complete(iotxn_t* txn, void* cookie);
    static void virtio_block_read(void* ctx, mx_handle_t vmo,
                                  uint64_t length, uint64_t vmo_offset,
                                  uint64_t dev_offset, void* cookie);
    static void virtio_block_write(void* ctx, mx_handle_t vmo,
                                   uint64_t length, uint64_t vmo_offset,
                                   uint64_t dev_offset, void* cookie);
    static void block_do_txn(BlockDevice* dev, uint32_t opcode, mx_handle_t vmo,
                             uint64_t length, uint64_t vmo_offset,
                             uint64_t dev_offset, void* cookie);

    void GetInfo(block_info_t* info);

    void QueueReadWriteTxn(iotxn_t* txn);

    // the main virtio ring
    Ring vring_ = {this};

    static const uint16_t ring_size = 128; // 128 matches legacy pci

    // saved block device configuration out of the pci config BAR
    virtio_blk_config_t config_ = {};

    // a queue of block request/responses
    static const size_t blk_req_count = 32;

    mx_paddr_t blk_req_pa_ = 0;
    virtio_blk_req_t* blk_req_ = nullptr;

    mx_paddr_t blk_res_pa_ = 0;
    uint8_t* blk_res_ = nullptr;

    uint32_t blk_req_bitmap_ = 0;
    static_assert(blk_req_count <= sizeof(blk_req_bitmap_) * CHAR_BIT, "");

    size_t alloc_blk_req() {
        size_t i = 0;
        if (blk_req_bitmap_ != 0)
            i = sizeof(blk_req_bitmap_) * CHAR_BIT - __builtin_clz(blk_req_bitmap_);
        blk_req_bitmap_ |= (1 << i);
        return i;
    }

    void free_blk_req(size_t i) {
        blk_req_bitmap_ &= ~(1 << i);
    }

    // Callbacks for PROTOCOL_BLOCK
    block_callbacks_t* callbacks_;
    block_protocol_ops_t device_block_ops_;

    // pending iotxns
    list_node iotxn_list = LIST_INITIAL_VALUE(iotxn_list);
};

} // namespace virtio
