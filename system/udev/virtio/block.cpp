// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <ddk/protocol/block.h>
#include <inttypes.h>
#include <magenta/compiler.h>
#include <mxtl/auto_lock.h>
#include <pretty/hexdump.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "trace.h"
#include "utils.h"

#define LOCAL_TRACE 0

// clang-format off
#define VIRTIO_BLK_F_BARRIER  (1<<0)
#define VIRTIO_BLK_F_SIZE_MAX (1<<1)
#define VIRTIO_BLK_F_SEG_MAX  (1<<2)
#define VIRTIO_BLK_F_GEOMETRY (1<<4)
#define VIRTIO_BLK_F_RO       (1<<5)
#define VIRTIO_BLK_F_BLK_SIZE (1<<6)
#define VIRTIO_BLK_F_SCSI     (1<<7)
#define VIRTIO_BLK_F_FLUSH    (1<<9)
#define VIRTIO_BLK_F_TOPOLOGY (1<<10)
#define VIRTIO_BLK_F_CONFIG_WCE (1<<11)

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2
// clang-format on

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

mx_status_t BlockDevice::virtio_block_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                        void* reply, size_t max, size_t* out_actual) {
    LTRACEF("ctx %p, op %u\n", ctx, op);

    BlockDevice* bd = static_cast<BlockDevice*>(ctx);

    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reinterpret_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return ERR_BUFFER_TOO_SMALL;
        memset(info, 0, sizeof(*info));
        info->block_size = bd->GetBlockSize();
        info->block_count = bd->GetSize() / bd->GetBlockSize();
        *out_actual = sizeof(*info);
        return NO_ERROR;
    }
    case IOCTL_BLOCK_RR_PART: {
        // rebind to reread the partition table
        return device_rebind(bd->device());
    }
    default:
        return ERR_NOT_SUPPORTED;
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
    auto err = vring_.Init(0, 128); // 128 matches legacy pci
    if (err < 0) {
        VIRTIO_ERROR("failed to allocate vring\n");
        return err;
    }

    // allocate a queue of block requests
    size_t size = sizeof(virtio_blk_req) * blk_req_count + sizeof(uint8_t) * blk_req_count;

    mx_status_t r = map_contiguous_memory(size, (uintptr_t*)&blk_req_, &blk_req_pa_);
    if (r < 0) {
        VIRTIO_ERROR("cannot alloc blk_req buffers %d\n", r);
        return r;
    }

    LTRACEF("allocated blk request at %p, physical address %#" PRIxPTR "\n", blk_req_, blk_req_pa_);

    // responses are 32 words at the end of the allocated block
    blk_res_pa_ = blk_req_pa_ + sizeof(virtio_blk_req) * blk_req_count;
    blk_res_ = (uint8_t*)((uintptr_t)blk_req_ + sizeof(virtio_blk_req) * blk_req_count);

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

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-block";
    args.ctx = this;
    args.ops = &device_ops_;
    args.proto_id = MX_PROTOCOL_BLOCK;

    auto status = device_add(bus_device_, &args, &device_);
    if (status < 0) {
        device_ = nullptr;
        return status;
    }

    return NO_ERROR;
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

#if LOCAL_TRACE > 0
            virtio_dump_desc(desc);
#endif

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
                list_delete(&txn->node);
                iotxn_complete(txn, NO_ERROR, txn->length);
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

void BlockDevice::QueueReadWriteTxn(iotxn_t* txn) {
    LTRACEF("txn %p\n", txn);

    mxtl::AutoLock lock(&lock_);

    bool write = (txn->opcode == IOTXN_OP_WRITE);

    // offset must be aligned to block size
    if (txn->offset % config_.blk_size) {
        TRACEF("offset %#" PRIx64 " is not aligned to sector size %u!\n", txn->offset, config_.blk_size);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // constrain to device capacity
    txn->length = MIN(txn->length, GetSize() - txn->offset);

    // allocate and start filling out a block request
    auto index = alloc_blk_req();
    LTRACEF("request index %u\n", index);
    auto req = &blk_req_[index];
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->ioprio = 0;
    req->sector = txn->offset / 512;
    LTRACEF("blk_req type %u ioprio %u sector %" PRIu64 "\n",
            req->type, req->ioprio, req->sector);

    /* put together a transfer */
    uint16_t i;
    auto desc = vring_.AllocDescChain(3, &i);
    LTRACEF("after alloc chain desc %p, i %u\n", desc, i);

    /* point the iotxn at this head descriptor */
    txn->context = desc;

    /* set up the descriptor pointing to the head */
    desc->addr = blk_req_pa_ + index * sizeof(virtio_blk_req);
    desc->len = sizeof(struct virtio_blk_req);
    desc->flags |= VRING_DESC_F_NEXT;

#if LOCAL_TRACE > 0
    virtio_dump_desc(desc);
#endif

    /* set up the descriptor pointing to the buffer */
    desc = vring_.DescFromIndex(desc->next);

    iotxn_physmap(txn);

    desc->addr = (uint64_t)iotxn_phys(txn);
    desc->len = (uint32_t)txn->length;

    if (!write)
        desc->flags |= VRING_DESC_F_WRITE; /* mark buffer as write-only if its a block read */
    desc->flags |= VRING_DESC_F_NEXT;

#if LOCAL_TRACE > 0
    virtio_dump_desc(desc);
#endif

    /* set up the descriptor pointing to the response */
    desc = vring_.DescFromIndex(desc->next);
    desc->addr = blk_res_pa_ + index;
    desc->len = 1;
    desc->flags = VRING_DESC_F_WRITE;

#if LOCAL_TRACE > 0
    virtio_dump_desc(desc);
#endif

    // save the iotxn in a list
    list_add_tail(&iotxn_list, &txn->node);

    /* submit the transfer */
    vring_.SubmitChain(i);

    /* kick it off */
    vring_.Kick();
}

} // namespace virtio
