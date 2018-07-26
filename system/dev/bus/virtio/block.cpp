// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <pretty/hexdump.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/compiler.h>

#include "trace.h"

#define LOCAL_TRACE 0

// 1MB max transfer (unless further restricted by ring size
#define MAX_SCATTER 257
#define MAX_MAX_XFER ((MAX_SCATTER - 1) * PAGE_SIZE)

#define PAGE_MASK (PAGE_SIZE - 1)

namespace virtio {

void BlockDevice::txn_complete(block_txn_t* txn, zx_status_t status) {
    if (txn->pmt != ZX_HANDLE_INVALID) {
        zx_pmt_unpin(txn->pmt);
        txn->pmt = ZX_HANDLE_INVALID;
    }
    txn->op.completion_cb(&txn->op, status);
}

// DDK level ops

// optional: return the size (in bytes) of the readable/writable space
// of the device.  Will default to 0 (non-seekable) if this is unimplemented
zx_off_t BlockDevice::virtio_block_get_size(void* ctx) {
    LTRACEF("ctx %p\n", ctx);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    return bd->GetSize();
}

void BlockDevice::GetInfo(block_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->block_size = GetBlockSize();
    info->block_count = GetSize() / GetBlockSize();
    info->max_transfer_size = (uint32_t)(PAGE_SIZE * (ring_size - 2));

    // limit max transfer to our worst case scatter list size
    if (info->max_transfer_size > MAX_MAX_XFER) {
        info->max_transfer_size = MAX_MAX_XFER;
    }
}

void BlockDevice::virtio_block_query(void* ctx, block_info_t* info, size_t* bopsz) {
    BlockDevice* bd = static_cast<BlockDevice*>(ctx);
    bd->GetInfo(info);
    *bopsz = sizeof(block_txn_t);
}

void BlockDevice::virtio_block_queue(void* ctx, block_op_t* bop) {
    BlockDevice* bd = static_cast<BlockDevice*>(ctx);
    block_txn_t* txn = static_cast<block_txn_t*>((void*) bop);
    txn->pmt = ZX_HANDLE_INVALID;

    switch(txn->op.command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
        bd->QueueReadWriteTxn(txn, false);
        break;
    case BLOCK_OP_WRITE:
        bd->QueueReadWriteTxn(txn, true);
        break;
    case BLOCK_OP_FLUSH:
        //TODO: this should complete after any in-flight IO and before
        //      any later IO begins
        bd->txn_complete(txn, ZX_OK);
        break;
    default:
        bd->txn_complete(txn, ZX_ERR_NOT_SUPPORTED);
    }

}

zx_status_t BlockDevice::virtio_block_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                            void* reply, size_t max, size_t* out_actual) {
    LTRACEF("ctx %p, op %u\n", ctx, op);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reinterpret_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        bd->GetInfo(info);
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC:
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

BlockDevice::BlockDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {
    sync_completion_reset(&txn_signal_);

    memset(&blk_req_buf_, 0, sizeof(blk_req_buf_));
}

BlockDevice::~BlockDevice() {
    io_buffer_release(&blk_req_buf_);
}

zx_status_t BlockDevice::Init() {
    LTRACE_ENTRY;

    // reset the device
    DeviceReset();

    // read our configuration
    CopyDeviceConfig(&config_, sizeof(config_));
    // TODO(cja): The blk_size provided in the device configuration is only
    // populated if a specific feature bit has been negotiated during
    // initialization, otherwise it is 0, at least in Virtio 0.9.5. Use 512
    // as a default as a stopgap for now until proper feature negotiation
    // is supported.
    if (config_.blk_size == 0)
        config_.blk_size = 512;

    LTRACEF("capacity %#" PRIx64 "\n", config_.capacity);
    LTRACEF("size_max %#x\n", config_.size_max);
    LTRACEF("seg_max  %#x\n", config_.seg_max);
    LTRACEF("blk_size %#x\n", config_.blk_size);

    // ack and set the driver status bit
    DriverStatusAck();

    // XXX check features bits and ack/nak them

    // allocate the main vring
    auto err = vring_.Init(0, ring_size);
    if (err < 0) {
        zxlogf(ERROR, "failed to allocate vring\n");
        return err;
    }

    // allocate a queue of block requests
    size_t size = sizeof(virtio_blk_req_t) * blk_req_count + sizeof(uint8_t) * blk_req_count;

    zx_status_t status = io_buffer_init(&blk_req_buf_, bti_.get(), size,
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "cannot alloc blk_req buffers %d\n", status);
        return status;
    }
    blk_req_ = static_cast<virtio_blk_req_t*>(io_buffer_virt(&blk_req_buf_));

    LTRACEF("allocated blk request at %p, physical address %#" PRIxPTR "\n", blk_req_,
            io_buffer_phys(&blk_req_buf_));

    // responses are 32 words at the end of the allocated block
    blk_res_pa_ = io_buffer_phys(&blk_req_buf_) + sizeof(virtio_blk_req_t) * blk_req_count;
    blk_res_ = (uint8_t*)((uintptr_t)blk_req_ + sizeof(virtio_blk_req_t) * blk_req_count);

    LTRACEF("allocated blk responses at %p, physical address %#" PRIxPTR "\n", blk_res_, blk_res_pa_);

    // start the interrupt thread
    StartIrqThread();

    // set DRIVER_OK
    DriverStatusOk();

    // initialize the zx_device and publish us
    // point the ctx of our DDK device at ourself
    device_ops_.get_size = &virtio_block_get_size;
    device_ops_.ioctl = &virtio_block_ioctl;

    block_ops_.query = &virtio_block_query;
    block_ops_.queue = &virtio_block_queue;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-block";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
    args.proto_ops = &block_ops_;

    status = device_add(bus_device_, &args, &device_);
    if (status != ZX_OK) {
        device_ = nullptr;
        return status;
    }

    return ZX_OK;
}

void BlockDevice::IrqRingUpdate() {
    LTRACE_ENTRY;

    // parse our descriptor chain, add back to the free queue
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint32_t i = (uint16_t)used_elem->id;
        struct vring_desc* desc = vring_.DescFromIndex((uint16_t)i);
        auto head_desc = desc; // save the first element
        {
            fbl::AutoLock lock(&ring_lock_);
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
        }

        bool need_signal = false;
        bool need_complete = false;
        block_txn_t* txn = nullptr;
        {
            fbl::AutoLock lock(&txn_lock_);

            // search our pending txn list to see if this completes it

            list_for_every_entry (&txn_list_, txn, block_txn_t, node) {
                if (txn->desc == head_desc) {
                    LTRACEF("completes txn %p\n", txn);
                    free_blk_req((unsigned int)txn->index);
                    list_delete(&txn->node);

                    // we will do this outside of the lock
                    need_complete = true;

                    // check to see if QueueTxn is waiting on
                    // resources becoming available
                    if ((need_signal = txn_wait_)) {
                        txn_wait_ = false;
                    }
                    break;
                }
            }
        }

        if (need_signal) {
            sync_completion_signal(&txn_signal_);
        }
        if (need_complete) {
            txn_complete(txn, ZX_OK);
        }
    };

    // tell the ring to find free chains and hand it back to our lambda
    vring_.IrqRingUpdate(free_chain);
}

void BlockDevice::IrqConfigChange() {
    LTRACE_ENTRY;
}

zx_status_t BlockDevice::QueueTxn(block_txn_t* txn, bool write, size_t bytes,
                                  uint64_t* pages, size_t pagecount, uint16_t* idx) {

    size_t index;
    {
        fbl::AutoLock lock(&txn_lock_);
        index = alloc_blk_req();
        if (index >= blk_req_count) {
            LTRACEF("too many block requests queued (%zu)!\n", index);
            return ZX_ERR_NO_RESOURCES;
        }
    }

    auto req = &blk_req_[index];
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->ioprio = 0;
    req->sector = txn->op.rw.offset_dev;
    LTRACEF("blk_req type %u ioprio %u sector %" PRIu64 "\n",
            req->type, req->ioprio, req->sector);

    // save the req index into the txn->extra[1] slot so we can free it when we complete the transfer
    txn->index = index;

#if LOCAL_TRACE
    LTRACEF("phys %p, phys_count %#lx\n", txn->phys, txn->phys_count);
    for (uint64_t i = 0; i < txn->phys_count; i++) {
        LTRACEF("phys %lu: %#lx\n", i, txn->phys[i]);
    }
#endif

    LTRACEF("page count %lu\n", pagecount);
    assert(pagecount > 0);

    /* put together a transfer */
    uint16_t i;
    vring_desc *desc;
    {
        fbl::AutoLock lock(&ring_lock_);
        desc = vring_.AllocDescChain((uint16_t)(2u + pagecount), &i);
    }
    if (!desc) {
        LTRACEF("failed to allocate descriptor chain of length %zu\n", 2u + pagecount);
        fbl::AutoLock lock(&txn_lock_);
        free_blk_req(index);
        return ZX_ERR_NO_RESOURCES;
    }

    LTRACEF("after alloc chain desc %p, i %u\n", desc, i);

    /* point the txn at this head descriptor */
    txn->desc = desc;

    /* set up the descriptor pointing to the head */
    desc->addr = io_buffer_phys(&blk_req_buf_) + index * sizeof(virtio_blk_req_t);
    desc->len = sizeof(virtio_blk_req_t);
    desc->flags = VRING_DESC_F_NEXT;
    LTRACE_DO(virtio_dump_desc(desc));

    for (size_t n = 0; n < pagecount; n++) {
        desc = vring_.DescFromIndex(desc->next);
        desc->addr = pages[n];
        desc->len = (uint32_t) ((bytes > PAGE_SIZE) ? PAGE_SIZE : bytes);
        if (n == 0) {
            // first entry may not be page aligned
            size_t page0_offset = txn->op.rw.offset_vmo & PAGE_MASK;

            // adjust starting address
            desc->addr += page0_offset;

            // trim length if necessary
            size_t max = PAGE_SIZE - page0_offset;
            if (desc->len > max) {
                desc->len = (uint32_t) max;
            }
        }
        desc->flags = VRING_DESC_F_NEXT;
        LTRACEF("pa %#lx, len %#x\n", desc->addr, desc->len);

        if (!write)
            desc->flags |= VRING_DESC_F_WRITE; /* mark buffer as write-only if its a block read */

        bytes -= desc->len;
    }
    LTRACE_DO(virtio_dump_desc(desc));
    assert(bytes == 0);

    /* set up the descriptor pointing to the response */
    desc = vring_.DescFromIndex(desc->next);
    desc->addr = blk_res_pa_ + index;
    desc->len = 1;
    desc->flags = VRING_DESC_F_WRITE;
    LTRACE_DO(virtio_dump_desc(desc));

    *idx = i;
    return ZX_OK;
}

void BlockDevice::QueueReadWriteTxn(block_txn_t* txn, bool write) {
    LTRACEF("txn %p, command %#x\n", txn, txn->op.command);

    fbl::AutoLock lock(&lock_);

    txn->op.rw.offset_vmo *= config_.blk_size;

    // transaction must fit within device
    if ((txn->op.rw.offset_dev >= config_.capacity) ||
        (config_.capacity - txn->op.rw.offset_dev < txn->op.rw.length)) {
        LTRACEF("request beyond the end of the device!\n");
        txn_complete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    if (txn->op.rw.length == 0) {
        txn_complete(txn, ZX_OK);
        return;
    }

    size_t bytes = txn->op.rw.length * config_.blk_size;

    uint64_t suboffset = txn->op.rw.offset_vmo & PAGE_MASK;
    uint64_t aligned_offset = txn->op.rw.offset_vmo & ~PAGE_MASK;
    size_t pin_size = ROUNDUP(suboffset + bytes, PAGE_SIZE);
    size_t num_pages = pin_size / PAGE_SIZE;
    if (num_pages > MAX_SCATTER) {
        TRACEF("virtio: transaction too large\n");
        txn_complete(txn, ZX_ERR_INVALID_ARGS);
        return;
    }

    zx_handle_t vmo = txn->op.rw.vmo;
    uint64_t pages[MAX_SCATTER];
    zx_status_t r;
    if ((r = zx_bti_pin(bti_.get(), ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo,
                        aligned_offset, pin_size, pages, num_pages, &txn->pmt)) != ZX_OK) {
        TRACEF("virtio: could not pin pages\n");
        txn_complete(txn, ZX_ERR_INTERNAL);
        return;
    }

    pages[0] += suboffset;

    bool cannot_fail = false;

    for (;;) {
        uint16_t idx;

        // attempt to setup hw txn
        zx_status_t status = QueueTxn(txn, write, bytes, pages, num_pages, &idx);
        if (status == ZX_OK) {
            fbl::AutoLock lock(&txn_lock_);

            // save the txn in a list
            list_add_tail(&txn_list_, &txn->node);

            /* submit the transfer */
            vring_.SubmitChain(idx);

            /* kick it off */
            vring_.Kick();

            return;
        } else {
            if (cannot_fail) {
                printf("virtio-block: failed to queue txn to hw: %d\n", status);
                txn_complete(txn, status);
                return;
            }

            fbl::AutoLock lock(&txn_lock_);

            if (list_is_empty(&txn_list_)) {
                // we hold the queue lock and the list is empty
                // if we fail this time around, no point in trying again
                cannot_fail = true;
                continue;
            } else {
                // let the completer know we need to wake up
                txn_wait_ = true;
            }
        }

        sync_completion_wait(&txn_signal_, ZX_TIME_INFINITE);
        sync_completion_reset(&txn_signal_);
    }
}

} // namespace virtio
