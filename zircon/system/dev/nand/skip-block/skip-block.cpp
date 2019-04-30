// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skip-block.h"

#include <string.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/badblock.h>
#include <ddk/protocol/nand.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>

#include <utility>

namespace nand {

namespace {

struct BlockOperationContext {
    ReadWriteOperation op;
    fuchsia_hardware_nand_Info* nand_info;
    LogicalToPhysicalMap* block_map;
    ddk::NandProtocolClient* nand;
    uint32_t copy;
    uint32_t current_block;
    uint32_t physical_block;
    sync_completion_t* completion_event;
    zx_status_t status;
    bool mark_bad;
};

// Called when all page reads in a block finish. If another block still needs
// to be read, it queues it up as another operation.
void ReadCompletionCallback(void* cookie, zx_status_t status, nand_operation_t* op) {
    auto* ctx = static_cast<BlockOperationContext*>(cookie);
    if (status != ZX_OK || ctx->current_block + 1 == ctx->op.block + ctx->op.block_count) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    ctx->current_block += 1;

    status = ctx->block_map->GetPhysical(ctx->copy, ctx->current_block, &ctx->physical_block);
    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }

    op->rw.offset_nand = ctx->physical_block * ctx->nand_info->pages_per_block;
    op->rw.offset_data_vmo += ctx->nand_info->pages_per_block;
    ctx->nand->Queue(op, ReadCompletionCallback, cookie);
    return;
}

void EraseCompletionCallback(void* cookie, zx_status_t status, nand_operation_t* op);

// Called when all page writes in a block finish. If another block still needs
// to be written, it queues up an erase.
void WriteCompletionCallback(void* cookie, zx_status_t status, nand_operation_t* op) {
    auto* ctx = static_cast<BlockOperationContext*>(cookie);

    if (status != ZX_OK || ctx->current_block + 1 == ctx->op.block + ctx->op.block_count) {
        ctx->status = status;
        ctx->mark_bad = (status == ZX_ERR_IO);
        sync_completion_signal(ctx->completion_event);
        return;
    }
    ctx->current_block += 1;
    ctx->op.vmo_offset += ctx->nand_info->pages_per_block;

    status = ctx->block_map->GetPhysical(ctx->copy, ctx->current_block, &ctx->physical_block);
    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = false;
        sync_completion_signal(ctx->completion_event);
        return;
    }
    op->erase.command = NAND_OP_ERASE;
    op->erase.first_block = ctx->physical_block;
    op->erase.num_blocks = 1;
    ctx->nand->Queue(op, EraseCompletionCallback, cookie);
    return;
}

// Called when a block erase operation finishes. Subsequently queues up writes
// to the block.
void EraseCompletionCallback(void* cookie, zx_status_t status, nand_operation_t* op) {
    auto* ctx = static_cast<BlockOperationContext*>(cookie);

    if (status != ZX_OK) {
        ctx->status = status;
        ctx->mark_bad = (status == ZX_ERR_IO);
        sync_completion_signal(ctx->completion_event);
        return;
    }
    op->rw.command = NAND_OP_WRITE;
    op->rw.data_vmo = ctx->op.vmo;
    op->rw.oob_vmo = ZX_HANDLE_INVALID;
    op->rw.length = ctx->nand_info->pages_per_block;
    op->rw.offset_nand = ctx->physical_block * ctx->nand_info->pages_per_block;
    op->rw.offset_data_vmo = ctx->op.vmo_offset;
    ctx->nand->Queue(op, WriteCompletionCallback, cookie);
    return;
}

// FIDL Message -> SkipBlockDevice translators.
zx_status_t GetPartitionInfo(void* ctx, fidl_txn_t* txn) {
    auto* device = reinterpret_cast<SkipBlockDevice*>(ctx);
    PartitionInfo info;
    zx_status_t status = device->GetPartitionInfo(&info);
    return fuchsia_hardware_skipblock_SkipBlockGetPartitionInfo_reply(txn, status, &info);
}

zx_status_t Read(void* ctx, const ReadWriteOperation* op, fidl_txn_t* txn) {
    auto* device = reinterpret_cast<SkipBlockDevice*>(ctx);
    zx_status_t status = device->Read(*op);
    return fuchsia_hardware_skipblock_SkipBlockRead_reply(txn, status);
}

zx_status_t Write(void* ctx, const ReadWriteOperation* op, fidl_txn_t* txn) {
    auto* device = reinterpret_cast<SkipBlockDevice*>(ctx);
    bool bad_block_grown;
    zx_status_t status = device->Write(*op, &bad_block_grown);
    return fuchsia_hardware_skipblock_SkipBlockWrite_reply(txn, status, bad_block_grown);
}

fuchsia_hardware_skipblock_SkipBlock_ops fidl_ops = {
    .GetPartitionInfo = GetPartitionInfo,
    .Read = Read,
    .Write = Write,
};

} // namespace

zx_status_t SkipBlockDevice::Create(zx_device_t* parent) {
    // Get NAND protocol.
    ddk::NandProtocolClient nand(parent);
    if (!nand.is_valid()) {
        zxlogf(ERROR, "skip-block: parent device '%s': does not support nand protocol\n",
               device_get_name(parent));
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Get bad block protocol.
    ddk::BadBlockProtocolClient bad_block(parent);
    if (!bad_block.is_valid()) {
        zxlogf(ERROR, "skip-block: parent device '%s': does not support bad_block protocol\n",
               device_get_name(parent));
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t copy_count;
    size_t actual;
    zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &copy_count,
                                             sizeof(copy_count), &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "skip-block: parent device '%s' has no private metadata\n",
               device_get_name(parent));
        return status;
    }
    if (actual != sizeof(copy_count)) {
        zxlogf(ERROR, "skip-block: Private metadata is of size %zu, expected to be %zu\n", actual,
               sizeof(copy_count));
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<SkipBlockDevice> device(new (&ac) SkipBlockDevice(parent, nand, bad_block,
                                                                      copy_count));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = device.release();
    return ZX_OK;
}

zx_status_t SkipBlockDevice::GetBadBlockList(fbl::Array<uint32_t>* bad_blocks) {
    size_t bad_block_count;
    zx_status_t status = bad_block_.GetBadBlockList(nullptr, 0, &bad_block_count);
    if (status != ZX_OK) {
        return status;
    }
    if (bad_block_count == 0) {
        bad_blocks->reset();
        return ZX_OK;
    }
    const size_t bad_block_list_len = bad_block_count;
    fbl::unique_ptr<uint32_t[]> bad_block_list(new uint32_t[bad_block_count]);
    memset(bad_block_list.get(), 0, sizeof(uint32_t) * bad_block_count);
    status = bad_block_.GetBadBlockList(bad_block_list.get(), bad_block_list_len, &bad_block_count);
    if (status != ZX_OK) {
        return status;
    }
    if (bad_block_list_len != bad_block_count) {
        return ZX_ERR_INTERNAL;
    }
    *bad_blocks = fbl::Array<uint32_t>(bad_block_list.release(), bad_block_count);
    return ZX_OK;
}

zx_status_t SkipBlockDevice::Bind() {
    zxlogf(INFO, "skip-block: Binding to %s\n", device_get_name(parent()));

    fbl::AutoLock al(&lock_);

    if (sizeof(nand_operation_t) > parent_op_size_) {
        zxlogf(ERROR, "skip-block: parent op size, %zu, is smaller than minimum op size: %zu\n",
               parent_op_size_, sizeof(nand_operation_t));
        return ZX_ERR_INTERNAL;
    }

    nand_op_ = NandOperation::Alloc(parent_op_size_);
    if (!nand_op_) {
        return ZX_ERR_NO_MEMORY;
    }

    // TODO(surajmalhotra): Potentially make this lazy instead of in the bind.
    fbl::Array<uint32_t> bad_blocks;
    const zx_status_t status = GetBadBlockList(&bad_blocks);
    if (status != ZX_OK) {
        zxlogf(ERROR, "skip-block: Failed to get bad block list\n");
        return status;
    }
    block_map_ = LogicalToPhysicalMap(copy_count_, nand_info_.num_blocks,
                                      std::move(bad_blocks));

    return DdkAdd("skip-block");
}

zx_status_t SkipBlockDevice::GetPartitionInfo(PartitionInfo* info) {
    fbl::AutoLock al(&lock_);

    info->block_size_bytes = GetBlockSize();
    uint32_t logical_block_count = UINT32_MAX;
    for (uint32_t copy = 0; copy < copy_count_; copy++) {
        logical_block_count = fbl::min(logical_block_count, block_map_.LogicalBlockCount(copy));
    }
    info->partition_block_count = logical_block_count;
    memcpy(info->partition_guid, nand_info_.partition_guid, ZBI_PARTITION_GUID_LEN);

    return ZX_OK;
}

zx_status_t SkipBlockDevice::ValidateVmo(const ReadWriteOperation& op) const {
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

zx_status_t SkipBlockDevice::Read(const ReadWriteOperation& op) {
    fbl::AutoLock al(&lock_);

    auto vmo = zx::vmo(op.vmo);
    zx_status_t status = ValidateVmo(op);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(surajmalhotra): We currently only read from the first copy. Given a
    // good use case, we could improve this to read from other copies in the
    // case or read failures, or perhaps expose ability to chose which copy gets
    // read to the user.
    constexpr uint32_t kReadCopy = 0;
    uint32_t physical_block;
    status = block_map_.GetPhysical(kReadCopy, op.block, &physical_block);
    if (status != ZX_OK) {
        return status;
    }
    sync_completion_t completion;
    BlockOperationContext op_context = {
        .op = op,
        .nand_info = &nand_info_,
        .block_map = &block_map_,
        .nand = &nand_,
        .copy = kReadCopy,
        .current_block = op.block,
        .physical_block = physical_block,
        .completion_event = &completion,
        .status = ZX_OK,
        .mark_bad = false,
    };

    nand_operation_t* nand_op = nand_op_->operation();
    nand_op->rw.command = NAND_OP_READ;
    nand_op->rw.data_vmo = op.vmo;
    nand_op->rw.oob_vmo = ZX_HANDLE_INVALID;
    nand_op->rw.length = nand_info_.pages_per_block;
    nand_op->rw.offset_nand = physical_block * nand_info_.pages_per_block;
    nand_op->rw.offset_data_vmo = op.vmo_offset;
    // The read callback will enqueue subsequent reads.
    nand_.Queue(nand_op, ReadCompletionCallback, &op_context);

    // Wait on completion.
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return op_context.status;
}

zx_status_t SkipBlockDevice::Write(const ReadWriteOperation& op, bool* bad_block_grown) {
    fbl::AutoLock al(&lock_);

    auto vmo = zx::vmo(op.vmo);
    zx_status_t status = ValidateVmo(op);
    if (status != ZX_OK) {
        return status;
    }

    *bad_block_grown = false;
    for (uint32_t copy = 0; copy < copy_count_; copy++) {
        for (;;) {
            uint32_t physical_block;
            status = block_map_.GetPhysical(copy, op.block, &physical_block);
            if (status != ZX_OK) {
                return status;
            }

            sync_completion_t completion;
            BlockOperationContext op_context = {
                .op = op,
                .nand_info = &nand_info_,
                .block_map = &block_map_,
                .nand = &nand_,
                .copy = copy,
                .current_block = op.block,
                .physical_block = physical_block,
                .completion_event = &completion,
                .status = ZX_OK,
                .mark_bad = false,
            };

            nand_operation_t* nand_op = nand_op_->operation();
            nand_op->erase.command = NAND_OP_ERASE;
            nand_op->erase.first_block = physical_block;
            nand_op->erase.num_blocks = 1;
            // The erase callback will enqueue subsequent writes and erases.
            nand_.Queue(nand_op, EraseCompletionCallback, &op_context);

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
                block_map_ = LogicalToPhysicalMap(
                    copy_count_, nand_info_.num_blocks, std::move(bad_blocks));
                *bad_block_grown = true;
                continue;
            }
            if (op_context.status != ZX_OK) {
                return op_context.status;
            }
            break;
        }
    }
    return ZX_OK;
}

zx_off_t SkipBlockDevice::DdkGetSize() {
    fbl::AutoLock al(&lock_);
    uint32_t logical_block_count = UINT32_MAX;
    for (uint32_t copy = 0; copy < copy_count_; copy++) {
        logical_block_count = fbl::min(logical_block_count, block_map_.LogicalBlockCount(copy));
    }
    return GetBlockSize() * logical_block_count;
}

zx_status_t SkipBlockDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_skipblock_SkipBlock_dispatch(this, txn, msg, &fidl_ops);
}

} // namespace nand

extern "C" zx_status_t skip_block_bind(void* ctx, zx_device_t* parent) {
    return nand::SkipBlockDevice::Create(parent);
}
