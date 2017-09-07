// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <ddk/protocol/block.h>
#include <inttypes.h>
#include <magenta/compiler.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "trace.h"
#include "utils.h"

#define LOCAL_TRACE 0

namespace virtio {

// DDK level ops

// queue an iotxn. iotxn's are always completed by its complete() op
void BlockDevice::virtio_block_iotxn_queue(void* ctx, iotxn_t* txn) {
    LTRACEF("ctx %p, txn %p\n", ctx, txn);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    switch (txn->opcode) {
    case IOTXN_OP_READ: {
        LTRACEF("READ offset %#" PRIx64 " length %#" PRIx64 "\n", txn->offset, txn->length);
        bd->QueueReadWriteTxn(txn);
        break;
    }
    case IOTXN_OP_WRITE:
        LTRACEF("WRITE offset %#" PRIx64 " length %#" PRIx64 "\n", txn->offset, txn->length);
        bd->QueueReadWriteTxn(txn);
        break;
    default:
        iotxn_complete(txn, -1, 0);
        break;
    }
}

// optional: return the size (in bytes) of the readable/writable space
// of the device.  Will default to 0 (non-seekable) if this is unimplemented
mx_off_t BlockDevice::virtio_block_get_size(void* ctx) {
    LTRACEF("ctx %p\n", ctx);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    return bd->GetSize();
}

void BlockDevice::GetInfo(block_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->block_size = GetBlockSize();
    info->block_count = GetSize() / GetBlockSize();
    info->max_transfer_size = (uint32_t)(PAGE_SIZE * (ring_size - 2));
}

mx_status_t BlockDevice::virtio_block_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                        void* reply, size_t max, size_t* out_actual) {
    LTRACEF("ctx %p, op %u\n", ctx, op);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reinterpret_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return MX_ERR_BUFFER_TOO_SMALL;
        bd->GetInfo(info);
        *out_actual = sizeof(*info);
        return MX_OK;
    }
    case IOCTL_BLOCK_RR_PART: {
        // rebind to reread the partition table
        return device_rebind(bd->device());
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

BlockDevice::BlockDevice(mx_device_t* bus_device)
    : Device(bus_device) {
    // so that Bind() knows how much io space to allocate
    bar0_size_ = 0x40;
}

BlockDevice::~BlockDevice() {
    // TODO: clean up allocated physical memory
}

void BlockDevice::virtio_block_set_callbacks(void* ctx, block_callbacks_t* cb) {
    BlockDevice* device = static_cast<BlockDevice*>(ctx);
    device->callbacks_ = cb;
}

void BlockDevice::virtio_block_get_info(void* ctx, block_info_t* info) {
    BlockDevice* device = static_cast<BlockDevice*>(ctx);
    device->GetInfo(info);
}

void BlockDevice::virtio_block_complete(iotxn_t* txn, void* cookie) {
    BlockDevice* dev = (BlockDevice *)txn->extra[0];
    dev->callbacks_->complete(cookie, txn->status);
    iotxn_release(txn);
}

void BlockDevice::block_do_txn(BlockDevice* dev, uint32_t opcode,
                               mx_handle_t vmo, uint64_t length,
                               uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    LTRACEF("vmo offset %#lx dev_offset %#lx length %#lx\n", vmo_offset, dev_offset, length);
    if ((dev_offset % dev->GetBlockSize()) || (length % dev->GetBlockSize())) {
        dev->callbacks_->complete(cookie, MX_ERR_INVALID_ARGS);
        return;
    }
    uint64_t size = dev->GetSize();
    if ((dev_offset >= size) || (length >= (size - dev_offset))) {
        dev->callbacks_->complete(cookie, MX_ERR_OUT_OF_RANGE);
        return;
    }

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL,
                    vmo, vmo_offset, length)) != MX_OK) {
        dev->callbacks_->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->length = length;
    txn->offset = dev_offset;
    txn->complete_cb = virtio_block_complete;
    txn->cookie = cookie;
    txn->extra[0] = (uint64_t)dev;

    iotxn_queue(dev->device_, txn);
}

void BlockDevice::virtio_block_read(void* ctx, mx_handle_t vmo,
                                    uint64_t length, uint64_t vmo_offset,
                                    uint64_t dev_offset, void* cookie) {
    block_do_txn((BlockDevice*)ctx, IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

void BlockDevice::virtio_block_write(void* ctx, mx_handle_t vmo,
                                     uint64_t length, uint64_t vmo_offset,
                                     uint64_t dev_offset, void* cookie) {
    block_do_txn((BlockDevice*)ctx, IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

mx_status_t BlockDevice::Init() {
    LTRACE_ENTRY;

    // reset the device
    Reset();

    // read our configuration
    CopyDeviceConfig(&config_, sizeof(config_));

    LTRACEF("capacity %#" PRIx64 "\n", config_.capacity);
    LTRACEF("size_max %#x\n", config_.size_max);
    LTRACEF("seg_max  %#x\n", config_.seg_max);
    LTRACEF("blk_size %#x\n", config_.blk_size);

    // ack and set the driver status bit
    StatusAcknowledgeDriver();

    // XXX check features bits and ack/nak them

    // allocate the main vring
    auto err = vring_.Init(0, ring_size);
    if (err < 0) {
        VIRTIO_ERROR("failed to allocate vring\n");
        return err;
    }

    // allocate a queue of block requests
    size_t size = sizeof(virtio_blk_req_t) * blk_req_count + sizeof(uint8_t) * blk_req_count;

    mx_status_t r = map_contiguous_memory(size, (uintptr_t*)&blk_req_, &blk_req_pa_);
    if (r < 0) {
        VIRTIO_ERROR("cannot alloc blk_req buffers %d\n", r);
        return r;
    }

    LTRACEF("allocated blk request at %p, physical address %#" PRIxPTR "\n", blk_req_, blk_req_pa_);

    // responses are 32 words at the end of the allocated block
    blk_res_pa_ = blk_req_pa_ + sizeof(virtio_blk_req_t) * blk_req_count;
    blk_res_ = (uint8_t*)((uintptr_t)blk_req_ + sizeof(virtio_blk_req_t) * blk_req_count);

    LTRACEF("allocated blk responses at %p, physical address %#" PRIxPTR "\n", blk_res_, blk_res_pa_);

    // start the interrupt thread
    StartIrqThread();

    // set DRIVER_OK
    StatusDriverOK();

    // initialize the mx_device and publish us
    // point the ctx of our DDK device at ourself
    device_ops_.iotxn_queue = &virtio_block_iotxn_queue;
    device_ops_.get_size = &virtio_block_get_size;
    device_ops_.ioctl = &virtio_block_ioctl;

    device_block_ops_.set_callbacks = &virtio_block_set_callbacks;
    device_block_ops_.get_info = &virtio_block_get_info;
    device_block_ops_.read = &virtio_block_read;
    device_block_ops_.write = &virtio_block_write;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-block";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = MX_PROTOCOL_BLOCK_CORE;
    args.proto_ops = &device_block_ops_;

    auto status = device_add(bus_device_, &args, &device_);
    if (status < 0) {
        device_ = nullptr;
        return status;
    }

    return MX_OK;
}

void BlockDevice::IrqRingUpdate() {
    LTRACE_ENTRY;

    // parse our descriptor chain, add back to the free queue
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint32_t i = (uint16_t)used_elem->id;
        struct vring_desc* desc = vring_.DescFromIndex((uint16_t)i);
        auto head_desc = desc; // save the first element
        for (;;) {
            int next;
            LTRACE_DO(virtio_dump_desc(desc));
            if (desc->flags & VRING_DESC_F_NEXT) {
                next = desc->next;
            } else {
                /* end of chain */
                next = -1;
            }

            vring_.FreeDesc((uint16_t)i);

            if (next < 0)
                break;
            i = next;
            desc = vring_.DescFromIndex((uint16_t)i);
        }

        // search our pending txn list to see if this completes it
        iotxn_t* txn;
        list_for_every_entry (&iotxn_list, txn, iotxn_t, node) {
            if (txn->context == head_desc) {
                LTRACEF("completes txn %p\n", txn);
                free_blk_req((unsigned int)txn->extra[1]);
                list_delete(&txn->node);
                iotxn_complete(txn, MX_OK, txn->length);
                break;
            }
        }
    };

    // tell the ring to find free chains and hand it back to our lambda
    vring_.IrqRingUpdate(free_chain);
}

void BlockDevice::IrqConfigChange() {
    LTRACE_ENTRY;
}

// given a iotxn, call back for every physical address run
// TODO: see if we can use the iotxn_phys_iter routines instead
template <typename callback>
static void ScatterGatherHelper(const iotxn_t *txn, callback callback_new_run) {
    uint64_t run_start = -1;
    uint64_t run_len = 0;

    uint64_t offset = (txn->vmo_offset % PAGE_SIZE);
    uint64_t remaining_len = txn->length;

    for (uint64_t i = 0; i < txn->phys_count; i++) {
        uint64_t page_offset = offset % PAGE_SIZE;

        if (txn->phys[i] != run_start + run_len) {
            // starting a new run, complete the old one first
            if (run_start != (uint64_t)-1)
                callback_new_run(run_start, run_len);

            run_start = txn->phys[i] + page_offset;
            run_len = 0;
        }

        run_len += fbl::min(PAGE_SIZE - page_offset, remaining_len);
        remaining_len -= fbl::min(PAGE_SIZE - page_offset, remaining_len);
        offset += PAGE_SIZE - page_offset;
    }

    // if there is still len left
    if (remaining_len > 0)
        run_len += remaining_len;

    // handle the last partial run
    if (run_len > 0)
        callback_new_run(run_start, run_len);
}

void BlockDevice::QueueReadWriteTxn(iotxn_t* txn) {
    LTRACEF("txn %p, pflags %#x\n", txn, txn->pflags);

    fbl::AutoLock lock(&lock_);

    bool write = (txn->opcode == IOTXN_OP_WRITE);

    // offset must be aligned to block size
    if (txn->offset % config_.blk_size) {
        LTRACEF("offset %#" PRIx64 " is not aligned to sector size %u!\n", txn->offset, config_.blk_size);
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }

    // trim length to a multiple of the block size
    if (txn->length % config_.blk_size) {
        txn->length = ROUNDDOWN(txn->length, config_.blk_size);
    }

    // constrain to device capacity
    txn->length = fbl::min(txn->length, GetSize() - txn->offset);
    if (txn->length == 0) {
        iotxn_complete(txn, MX_OK, 0);
        return;
    }

    // allocate and start filling out a block request
    auto index = alloc_blk_req();
    if (index >= blk_req_count) {
        TRACEF("too many block requests queued (%zu)!\n", index);
        iotxn_complete(txn, MX_ERR_NO_RESOURCES, 0);
        return;
    }

    auto req = &blk_req_[index];
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->ioprio = 0;
    req->sector = txn->offset / 512;
    LTRACEF("blk_req type %u ioprio %u sector %" PRIu64 "\n",
            req->type, req->ioprio, req->sector);

    // save the req index into the txn->extra[1] slot so we can free it when we complete the transfer
    txn->extra[1] = index;

    // get the physical map for the transfer
    auto status = iotxn_physmap(txn);
    LTRACEF("status %d, pflags %#x\n", status, txn->pflags);
#if LOCAL_TRACE
    LTRACEF("phys %p, phys_count %#lx\n", txn->phys, txn->phys_count);
    for (uint64_t i = 0; i < txn->phys_count; i++) {
        LTRACEF("phys %lu: %#lx\n", i, txn->phys[i]);
    }
#endif

    // count the number of physical runs we're going to need
    size_t run_count = 0;

    {
        auto new_run_callback = [&run_count](uint64_t start, uint64_t len) {
            LTRACEF("start %#lx len %#lx\n", start, len);
            run_count++;
        };

        ScatterGatherHelper(txn, new_run_callback);
    }

    LTRACEF("run count %lu\n", run_count);
    assert(run_count > 0);

    /* put together a transfer */
    uint16_t i;
    auto desc = vring_.AllocDescChain((uint16_t)(2u + run_count), &i);
    if (!desc) {
        TRACEF("failed to allocate descriptor chain of length %zu\n", 2u + run_count);
        // TODO: handle this scenario by requeing the transfer in smaller runs
        iotxn_complete(txn, MX_ERR_NO_RESOURCES, 0);
        return;
    }

    LTRACEF("after alloc chain desc %p, i %u\n", desc, i);

    /* point the iotxn at this head descriptor */
    txn->context = desc;

    /* set up the descriptor pointing to the head */
    desc->addr = blk_req_pa_ + index * sizeof(virtio_blk_req_t);
    desc->len = sizeof(virtio_blk_req_t);
    desc->flags |= VRING_DESC_F_NEXT;
    LTRACE_DO(virtio_dump_desc(desc));
    {
        auto new_run_callback = [this, write, &desc](uint64_t start, uint64_t len) {
            /* set up the descriptor pointing to the buffer */
            desc = vring_.DescFromIndex(desc->next);

            desc->addr = start;
            desc->len = (uint32_t)len;
            LTRACEF("pa %#lx, len %#x\n", desc->addr, desc->len);

            if (!write)
                desc->flags |= VRING_DESC_F_WRITE; /* mark buffer as write-only if its a block read */
            desc->flags |= VRING_DESC_F_NEXT;
        };

        ScatterGatherHelper(txn, new_run_callback);
    }
    LTRACE_DO(virtio_dump_desc(desc));

    /* set up the descriptor pointing to the response */
    desc = vring_.DescFromIndex(desc->next);
    desc->addr = blk_res_pa_ + index;
    desc->len = 1;
    desc->flags = VRING_DESC_F_WRITE;
    LTRACE_DO(virtio_dump_desc(desc));

    // save the iotxn in a list
    list_add_tail(&iotxn_list, &txn->node);

    /* submit the transfer */
    vring_.SubmitChain(i);

    /* kick it off */
    vring_.Kick();
}

} // namespace virtio
