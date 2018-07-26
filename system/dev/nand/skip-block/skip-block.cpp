// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skip-block.h"

#include <string.h>

#include <ddk/debug.h>
#include <ddk/protocol/bad-block.h>
#include <ddk/protocol/nand.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmo.h>
#include <lib/sync/completion.h>
#include <zircon/boot/image.h>

namespace nand {

namespace {

struct BlockOperationContext {
    skip_block_rw_operation_t op;
    nand_info_t* nand_info;
    LogicalToPhysicalMap* block_map;
    ddk::NandProtocolProxy* nand;
    uint32_t current_block;
    uint32_t physical_block;
    sync_completion_t* completion_event;
    zx_status_t status;
    bool mark_bad;
};

// Called when all page reads in a block finish. If another block still needs
// to be read, it queues it up as another operation.
void ReadCompletionCallback(nand_op_t* op, zx_status_t status) {
    auto* ctx = static_cast<BlockOperationContext*>(op->cookie);
    if (status != ZX_OK || ctx->current_block + 1 == ctx->op.block + ctx->op.block_count) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    ctx->current_block += 1;

    status = ctx->block_map->GetPhysical(ctx->current_block, &ctx->physical_block);
    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }

    op->rw.offset_nand = ctx->physical_block * ctx->nand_info->pages_per_block;
    op->rw.offset_data_vmo += ctx->nand_info->pages_per_block;
    ctx->nand->Queue(op);
    return;
}

void EraseCompletionCallback(nand_op_t* op, zx_status_t status);

// Called when all page writes in a block finish. If another block still needs
// to be written, it queues up an erase.
void WriteCompletionCallback(nand_op_t* op, zx_status_t status) {
    auto* ctx = static_cast<BlockOperationContext*>(op->cookie);

    if (status != ZX_OK || ctx->current_block + 1 == ctx->op.block + ctx->op.block_count) {
        ctx->status = status;
        ctx->mark_bad = status != ZX_OK;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    ctx->current_block += 1;
    ctx->op.vmo_offset += ctx->nand_info->pages_per_block;

    status = ctx->block_map->GetPhysical(ctx->current_block, &ctx->physical_block);
    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    op->erase.command = NAND_OP_ERASE;
    op->erase.first_block = ctx->physical_block;
    op->erase.num_blocks = 1;
    op->completion_cb = EraseCompletionCallback;
    ctx->nand->Queue(op);
    return;
}

// Called when a block erase operation finishes. Subsequently queues up writes
// to the block.
void EraseCompletionCallback(nand_op_t* op, zx_status_t status) {
    auto* ctx = static_cast<BlockOperationContext*>(op->cookie);

    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = true;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    op->rw.command = NAND_OP_WRITE;
    op->rw.data_vmo = ctx->op.vmo;
    op->rw.oob_vmo = ZX_HANDLE_INVALID;
    op->rw.length = ctx->nand_info->pages_per_block;
    op->rw.offset_nand = ctx->physical_block * ctx->nand_info->pages_per_block;
    op->rw.offset_data_vmo = ctx->op.vmo_offset;
    op->rw.pages = nullptr;
    op->completion_cb = WriteCompletionCallback;
    ctx->nand->Queue(op);
    return;
}

} // namespace

zx_status_t SkipBlockDevice::Create(zx_device_t* parent) {
    // Get NAND protocol.
    nand_protocol_t nand_proto;
    if (device_get_protocol(parent, ZX_PROTOCOL_NAND, &nand_proto) != ZX_OK) {
        zxlogf(ERROR, "skip-block: parent device '%s': does not support nand protocol\n",
               device_get_name(parent));
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Get bad block protocol.
    bad_block_protocol_t bad_block_proto;
    if (device_get_protocol(parent, ZX_PROTOCOL_BAD_BLOCK, &bad_block_proto) != ZX_OK) {
        zxlogf(ERROR, "skip-block: parent device '%s': does not support bad_block protocol\n",
               device_get_name(parent));
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<SkipBlockDevice> device(new (&ac) SkipBlockDevice(parent, nand_proto,
                                                                      bad_block_proto));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = device.release();
    return ZX_OK;
}

zx_status_t SkipBlockDevice::GetBadBlockList(fbl::Array<uint32_t>* bad_blocks) {
    uint32_t bad_block_count;
    zx_status_t status = bad_block_.GetBadBlockList(nullptr, 0, &bad_block_count);
    if (status != ZX_OK) {
        return status;
    }
    if (bad_block_count == 0) {
        bad_blocks->reset();
        return ZX_OK;
    }
    const uint32_t bad_block_list_len = bad_block_count;
    fbl::unique_ptr<uint32_t[]> bad_block_list(new uint32_t[bad_block_count]);
    status = bad_block_.GetBadBlockList(bad_block_list.get(), bad_block_list_len, &bad_block_count);
    if (status != ZX_OK) {
        return status;
    }
    if (bad_block_list_len != bad_block_count) {
        return ZX_ERR_INTERNAL;
    }
    *bad_blocks = fbl::move(fbl::Array<uint32_t>(bad_block_list.release(), bad_block_count));
    return ZX_OK;
}

zx_status_t SkipBlockDevice::Bind() {
    zxlogf(INFO, "skip-block: Binding to %s\n", device_get_name(parent()));

    fbl::AutoLock al(&lock_);

    if (sizeof(nand_op_t) > parent_op_size_) {
        zxlogf(ERROR, "skip-block: parent op size, %zu, is smaller than minimum op size: %zu\n",
               sizeof(nand_op_t), parent_op_size_);
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    fbl::Array<uint8_t> nand_op(new (&ac) uint8_t[parent_op_size_], parent_op_size_);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    nand_op_ = fbl::move(nand_op);

    // TODO(surajmalhotra): Potentially make this lazy instead of in the bind.
    fbl::Array<uint32_t> bad_blocks;
    const zx_status_t status = GetBadBlockList(&bad_blocks);
    if (status != ZX_OK) {
        zxlogf(ERROR, "skip-block: Failed to get bad block list\n");
        return status;
    }
    block_map_ = fbl::move(LogicalToPhysicalMap(nand_info_.num_blocks, fbl::move(bad_blocks)));

    return DdkAdd("skip-block");
}

zx_status_t SkipBlockDevice::GetPartitionInfo(skip_block_partition_info_t* info) const {
    info->block_size_bytes = GetBlockSize();
    info->partition_block_count = block_map_.LogicalBlockCount();
    memcpy(info->partition_guid, nand_info_.partition_guid, ZBI_PARTITION_GUID_LEN);

    return ZX_OK;
}

zx_status_t SkipBlockDevice::ValidateVmo(const skip_block_rw_operation_t& op) const {
    uint64_t vmo_size;

    zx_status_t status = zx_vmo_get_size(op.vmo, &vmo_size);
    if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (vmo_size < op.vmo_offset + op.block_count * GetBlockSize()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
}

zx_status_t SkipBlockDevice::Read(const skip_block_rw_operation_t& op) {
    auto vmo = zx::vmo(op.vmo);
    zx_status_t status = ValidateVmo(op);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t physical_block;
    status = block_map_.GetPhysical(op.block, &physical_block);
    if (status != ZX_OK) {
        return status;
    }
    sync_completion_t completion;
    BlockOperationContext op_context = {
        .op = op,
        .nand_info = &nand_info_,
        .block_map = &block_map_,
        .nand = &nand_,
        .current_block = op.block,
        .physical_block = physical_block,
        .completion_event = &completion,
        .status = ZX_OK,
        .mark_bad = false,
    };

    auto* nand_op = reinterpret_cast<nand_op_t*>(nand_op_.get());
    nand_op->rw.command = NAND_OP_READ;
    nand_op->rw.data_vmo = op.vmo;
    nand_op->rw.oob_vmo = ZX_HANDLE_INVALID;
    nand_op->rw.length = nand_info_.pages_per_block;
    nand_op->rw.offset_nand = physical_block * nand_info_.pages_per_block;
    nand_op->rw.offset_data_vmo = op.vmo_offset;
    // The read callback will enqueue subsequent reads.
    nand_op->completion_cb = ReadCompletionCallback;
    nand_op->cookie = &op_context;
    nand_.Queue(nand_op);

    // Wait on completion.
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return op_context.status;
}

zx_status_t SkipBlockDevice::Write(const skip_block_rw_operation_t& op, bool* bad_block_grown) {
    auto vmo = zx::vmo(op.vmo);
    zx_status_t status = ValidateVmo(op);
    if (status != ZX_OK) {
        return status;
    }

    *bad_block_grown = false;
    for (;;) {
        uint32_t physical_block;
        status = block_map_.GetPhysical(op.block, &physical_block);
        if (status != ZX_OK) {
            return status;
        }

        sync_completion_t completion;
        BlockOperationContext op_context = {
            .op = op,
            .nand_info = &nand_info_,
            .block_map = &block_map_,
            .nand = &nand_,
            .current_block = op.block,
            .physical_block = physical_block,
            .completion_event = &completion,
            .status = ZX_OK,
            .mark_bad = false,
        };

        auto* nand_op = reinterpret_cast<nand_op_t*>(nand_op_.get());
        nand_op->erase.command = NAND_OP_ERASE;
        nand_op->erase.first_block = physical_block;
        nand_op->erase.num_blocks = 1;
        // The erase callback will enqueue subsequent writes and erases.
        nand_op->completion_cb = EraseCompletionCallback;
        nand_op->cookie = &op_context;
        nand_.Queue(nand_op);

        // Wait on completion.
        sync_completion_wait(&completion, ZX_TIME_INFINITE);
        if (op_context.mark_bad) {
            zxlogf(ERROR, "Failed to erase/write block %u, marking bad\n",
                   op_context.physical_block);
            status = bad_block_.MarkBlockBad(op_context.physical_block);
            if (status != ZX_OK) {
                zxlogf(ERROR, "skip-block: Failed to mark block bad\n");
                return status;
            }
            // Logical to physical mapping has changed, so we need to re-initialize block_map_.
            fbl::Array<uint32_t> bad_blocks;
            // TODO(surajmalhotra): Make it impossible for this to fail.
            ZX_ASSERT(GetBadBlockList(&bad_blocks) == ZX_OK);
            block_map_ = fbl::move(LogicalToPhysicalMap(nand_info_.num_blocks,
                                                        fbl::move(bad_blocks)));
            *bad_block_grown = true;
            continue;
        }
        return op_context.status;
    }
}

zx_status_t SkipBlockDevice::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    fbl::AutoLock lock(&lock_);

    zxlogf(TRACE, "skip-block: IOCTL %x\n", op);

    switch (op) {
    case IOCTL_SKIP_BLOCK_GET_PARTITION_INFO:
        if (!out_buf || out_len < sizeof(skip_block_partition_info_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        *out_actual = sizeof(skip_block_partition_info_t);
        return GetPartitionInfo(static_cast<skip_block_partition_info_t*>(out_buf));

    case IOCTL_SKIP_BLOCK_READ:
        if (!in_buf || in_len < sizeof(skip_block_rw_operation_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return Read(*static_cast<const skip_block_rw_operation_t*>(in_buf));

    case IOCTL_SKIP_BLOCK_WRITE: {
        if (!in_buf || in_len < sizeof(skip_block_rw_operation_t) ||
            !out_buf || out_len < sizeof(bool)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_status_t status = Write(*static_cast<const skip_block_rw_operation_t*>(in_buf),
                                   static_cast<bool*>(out_buf));
        if (status == ZX_OK) {
            *out_actual = sizeof(bool);
        }
        return status;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace nand

extern "C" zx_status_t skip_block_bind(void* ctx, zx_device_t* parent) {
    return nand::SkipBlockDevice::Create(parent);
}
