// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skip-block.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/badblock.h>
#include <ddk/protocol/nand.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

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
  const size_t vmo_offset =
      ctx->op.vmo_offset + ((ctx->current_block - ctx->op.block) * ctx->nand_info->pages_per_block);
  op->rw.command = NAND_OP_WRITE;
  op->rw.data_vmo = ctx->op.vmo.get();
  op->rw.oob_vmo = ZX_HANDLE_INVALID;
  op->rw.length = ctx->nand_info->pages_per_block;
  op->rw.offset_nand = ctx->physical_block * ctx->nand_info->pages_per_block;
  op->rw.offset_data_vmo = vmo_offset;
  ctx->nand->Queue(op, WriteCompletionCallback, cookie);
  return;
}

}  // namespace

zx_status_t SkipBlockDevice::Create(void*, zx_device_t* parent) {
  // Get NAND protocol.
  ddk::NandProtocolClient nand(parent);
  if (!nand.is_valid()) {
    zxlogf(ERROR, "skip-block: parent device '%s': does not support nand protocol",
           device_get_name(parent));
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Get bad block protocol.
  ddk::BadBlockProtocolClient bad_block(parent);
  if (!bad_block.is_valid()) {
    zxlogf(ERROR, "skip-block: parent device '%s': does not support bad_block protocol",
           device_get_name(parent));
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t copy_count;
  size_t actual;
  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &copy_count,
                                           sizeof(copy_count), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "skip-block: parent device '%s' has no private metadata",
           device_get_name(parent));
    return status;
  }
  if (actual != sizeof(copy_count)) {
    zxlogf(ERROR, "skip-block: Private metadata is of size %zu, expected to be %zu", actual,
           sizeof(copy_count));
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<SkipBlockDevice> device(new (&ac)
                                              SkipBlockDevice(parent, nand, bad_block, copy_count));
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
  std::unique_ptr<uint32_t[]> bad_block_list(new uint32_t[bad_block_count]);
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
  zxlogf(INFO, "skip-block: Binding to %s", device_get_name(parent()));

  fbl::AutoLock al(&lock_);

  if (sizeof(nand_operation_t) > parent_op_size_) {
    zxlogf(ERROR, "skip-block: parent op size, %zu, is smaller than minimum op size: %zu",
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
    zxlogf(ERROR, "skip-block: Failed to get bad block list");
    return status;
  }
  block_map_ = LogicalToPhysicalMap(copy_count_, nand_info_.num_blocks, std::move(bad_blocks));

  return DdkAdd("skip-block");
}

void SkipBlockDevice::GetPartitionInfo(GetPartitionInfoCompleter::Sync completer) {
  fbl::AutoLock al(&lock_);

  PartitionInfo info;
  info.block_size_bytes = GetBlockSize();
  info.partition_block_count = GetBlockCountLocked();
  memcpy(info.partition_guid.data(), nand_info_.partition_guid, ZBI_PARTITION_GUID_LEN);

  completer.Reply(ZX_OK, info);
}

zx_status_t SkipBlockDevice::ValidateOperationLocked(const ReadWriteOperation& op) const {
  if (op.block_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (op.block + op.block_count > GetBlockCountLocked()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t vmo_size;
  zx_status_t status = op.vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmo_size < op.vmo_offset + op.block_count * GetBlockSize()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

zx_status_t SkipBlockDevice::ValidateOperationLocked(const WriteBytesOperation& op) const {
  if (op.size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (op.offset % nand_info_.page_size != 0 || op.size % nand_info_.page_size != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (fbl::round_up(op.offset + op.size, GetBlockSize()) > GetBlockCountLocked() * GetBlockSize()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t vmo_size;
  zx_status_t status = op.vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmo_size < op.vmo_offset + op.size) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

zx_status_t SkipBlockDevice::ReadLocked(ReadWriteOperation op) {
  for (uint32_t copy = 0; copy < copy_count_; copy++) {
    if (block_map_.AvailableBlockCount(copy) < op.block_count) {
      zxlogf(INFO, "skipblock: copy %u too small, skipping read attempt.", copy);
      continue;
    }

    uint32_t physical_block;
    zx_status_t status = block_map_.GetPhysical(copy, op.block, &physical_block);
    if (status != ZX_OK) {
      return status;
    }
    sync_completion_t completion;
    BlockOperationContext op_context = {
        .op = std::move(op),
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
    nand_op->rw.command = NAND_OP_READ;
    nand_op->rw.data_vmo = op_context.op.vmo.get();
    nand_op->rw.oob_vmo = ZX_HANDLE_INVALID;
    nand_op->rw.length = nand_info_.pages_per_block;
    nand_op->rw.offset_nand = physical_block * nand_info_.pages_per_block;
    nand_op->rw.offset_data_vmo = op.vmo_offset;
    // The read callback will enqueue subsequent reads.
    nand_.Queue(nand_op, ReadCompletionCallback, &op_context);

    // Wait on completion.
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    op = std::move(op_context.op);
    if (op_context.status == ZX_OK) {
      if (copy != 0) {
        zxlogf(INFO, "skipblock: Successfully read block %d, copy %d", op_context.current_block,
               copy);
      }
      return ZX_OK;
    }
    zxlogf(WARN, "skipblock: Failed to read block %d, copy %d, with status %s",
           op_context.current_block, copy, zx_status_get_string(op_context.status));
  }
  zxlogf(ERROR, "skipblock: Failed to read any copies of block %d", op.block);
  return ZX_ERR_IO;
}

void SkipBlockDevice::Read(ReadWriteOperation op, ReadCompleter::Sync completer) {
  fbl::AutoLock al(&lock_);

  zx_status_t status = ValidateOperationLocked(op);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }
  completer.Reply(ReadLocked(std::move(op)));
}

zx_status_t SkipBlockDevice::WriteLocked(ReadWriteOperation op, bool* bad_block_grown) {
  *bad_block_grown = false;

  bool one_copy_succeeded = false;
  for (uint32_t copy = 0; copy < copy_count_; copy++) {
    for (;;) {
      if (op.block >= block_map_.AvailableBlockCount(copy)) {
        break;
      }
      uint32_t physical_block;
      zx_status_t status = block_map_.GetPhysical(copy, op.block, &physical_block);
      if (status != ZX_OK) {
        return status;
      }

      sync_completion_t completion;
      BlockOperationContext op_context = {
          .op = std::move(op),
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
      op = std::move(op_context.op);
      if (op_context.mark_bad) {
        zxlogf(ERROR, "Failed to erase/write block %u, marking bad", op_context.physical_block);
        status = bad_block_.MarkBlockBad(op_context.physical_block);
        if (status != ZX_OK) {
          zxlogf(ERROR, "skip-block: Failed to mark block bad");
          return status;
        }
        // Logical to physical mapping has changed, so we need to re-initialize block_map_.
        fbl::Array<uint32_t> bad_blocks;
        // TODO(surajmalhotra): Make it impossible for this to fail.
        ZX_ASSERT(GetBadBlockList(&bad_blocks) == ZX_OK);
        block_map_ =
            LogicalToPhysicalMap(copy_count_, nand_info_.num_blocks, std::move(bad_blocks));
        *bad_block_grown = true;
        continue;
      }
      if (op_context.status != ZX_OK) {
        zxlogf(ERROR, "Failed to write block %d, copy %d with status %s",
               op_context.current_block, copy, zx_status_get_string(op_context.status));
        break;
      }
      one_copy_succeeded = true;
      break;
    }
  }
  if (!one_copy_succeeded) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void SkipBlockDevice::Write(ReadWriteOperation op, WriteCompleter::Sync completer) {
  fbl::AutoLock al(&lock_);

  bool bad_block_grown = false;
  zx_status_t status = ValidateOperationLocked(op);
  if (status != ZX_OK) {
    completer.Reply(status, bad_block_grown);
    return;
  }

  status = WriteLocked(std::move(op), &bad_block_grown);
  completer.Reply(status, bad_block_grown);
}

zx_status_t SkipBlockDevice::ReadPartialBlocksLocked(WriteBytesOperation op, uint64_t block_size,
                                                     uint64_t first_block, uint64_t last_block,
                                                     uint64_t op_size, zx::vmo* vmo) {
  zx_status_t status = zx::vmo::create(op_size, 0, vmo);
  if (status != ZX_OK) {
    return status;
  }

  if (op.offset % block_size) {
    // Need to read first block.
    zx::vmo dup;
    status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
      return status;
    }
    ReadWriteOperation rw_op = {
        .vmo = std::move(dup),
        .vmo_offset = 0,
        .block = static_cast<uint32_t>(first_block),
        .block_count = 1,
    };
    status = ReadLocked(std::move(rw_op));
    if (status != ZX_OK) {
      return status;
    }
  }

  if ((first_block != last_block || op.offset % block_size == 0) &&
      (op.offset + op.size) % block_size != 0) {
    // Need to read last block.
    zx::vmo dup;
    status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
      return status;
    }
    ReadWriteOperation rw_op = {
        .vmo = std::move(dup),
        .vmo_offset = last_block * block_size,
        .block = static_cast<uint32_t>(last_block),
        .block_count = 1,
    };
    status = ReadLocked(std::move(rw_op));
    if (status != ZX_OK) {
      return status;
    }
  }

  // Copy from input vmo to newly created one.
  fzl::VmoMapper mapper;
  const size_t vmo_page_offset = op.vmo_offset % ZX_PAGE_SIZE;
  status = mapper.Map(op.vmo, fbl::round_down(op.vmo_offset, ZX_PAGE_SIZE),
                      fbl::round_up(vmo_page_offset + op.size, ZX_PAGE_SIZE), ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    return status;
  }

  status = vmo->write(static_cast<uint8_t*>(mapper.start()) + vmo_page_offset,
                      op.offset % block_size, op.size);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

void SkipBlockDevice::WriteBytes(WriteBytesOperation op, WriteBytesCompleter::Sync completer) {
  fbl::AutoLock al(&lock_);

  bool bad_block_grown = false;
  zx_status_t status = ValidateOperationLocked(op);
  if (status != ZX_OK) {
    completer.Reply(status, bad_block_grown);
    return;
  }

  const uint64_t block_size = GetBlockSize();
  const uint64_t first_block = op.offset / block_size;
  const uint64_t last_block = fbl::round_up(op.offset + op.size, block_size) / block_size - 1;
  const uint64_t op_size = (last_block - first_block + 1) * block_size;

  zx::vmo vmo;
  if (op_size == op.size) {
    // No copies are necessary as offset and size are block aligned.
    vmo = std::move(op.vmo);
  } else {
    status =
        ReadPartialBlocksLocked(std::move(op), block_size, first_block, last_block, op_size, &vmo);
    if (status != ZX_OK) {
      completer.Reply(status, bad_block_grown);
      return;
    }
  }

  // Now issue normal write.
  ReadWriteOperation rw_op = {
      .vmo = std::move(vmo),
      .vmo_offset = 0,
      .block = static_cast<uint32_t>(first_block),
      .block_count = static_cast<uint32_t>(last_block - first_block + 1),
  };
  status = WriteLocked(std::move(rw_op), &bad_block_grown);
  completer.Reply(status, bad_block_grown);
}

uint32_t SkipBlockDevice::GetBlockCountLocked() const {
  uint32_t logical_block_count = 0;
  for (uint32_t copy = 0; copy < copy_count_; copy++) {
    logical_block_count = std::max(logical_block_count, block_map_.AvailableBlockCount(copy));
  }
  return logical_block_count;
}

zx_off_t SkipBlockDevice::DdkGetSize() {
  fbl::AutoLock al(&lock_);
  return GetBlockSize() * GetBlockCountLocked();
}

zx_status_t SkipBlockDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::skipblock::SkipBlock::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SkipBlockDevice::Create;
  return ops;
}();

}  // namespace nand

// clang-format off
ZIRCON_DRIVER_BEGIN(skip_block, nand::driver_ops, "zircon", "0.1", 5)
    BI_GOTO_IF(NE, BIND_AUTOBIND, 0, 758),
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_NAND),
    BI_LABEL(758),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_NAND),
    BI_MATCH_IF(EQ, BIND_NAND_CLASS, fuchsia_hardware_nand_Class_BBS),
ZIRCON_DRIVER_END(skip_block)
    // clang-format on
