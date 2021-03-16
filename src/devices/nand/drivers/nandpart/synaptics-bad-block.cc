// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "synaptics-bad-block.h"

#include <lib/ddk/debug.h>
#include <lib/sync/completion.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

namespace {

struct BlockOperationContext {
  sync_completion_t* completion_event;
  zx_status_t status;
};

constexpr uint32_t kBitsPerEntry = 2;
constexpr uint32_t kEntriesPerByte = 8 / kBitsPerEntry;

constexpr uint8_t kEntryMask = 0x03;

constexpr uint8_t kEntryBlockBad = 0x01;
constexpr uint8_t kEntryBlockGood = 0x03;

bool IsBadBlock(const fbl::Array<uint8_t>& bbt_contents, uint32_t block) {
  const uint32_t index = block / kEntriesPerByte;
  ZX_DEBUG_ASSERT(bbt_contents.size() > index);

  const uint32_t shift = (block - (index * kEntriesPerByte)) * kBitsPerEntry;

  return ((bbt_contents[index] >> shift) & kEntryMask) != kEntryBlockGood;
}

void SetBlockBad(fbl::Array<uint8_t>* bbt_contents, uint32_t block) {
  const uint32_t index = block / kEntriesPerByte;
  ZX_DEBUG_ASSERT(bbt_contents->size() > index);

  const uint32_t shift = (block - (index * kEntriesPerByte)) * kBitsPerEntry;

  (*bbt_contents)[index] = static_cast<uint8_t>((*bbt_contents)[index] & ~(kEntryMask << shift));
  (*bbt_contents)[index] = static_cast<uint8_t>((*bbt_contents)[index] | (kEntryBlockBad << shift));
}

void CompletionCallback(void* cookie, zx_status_t status, nand_operation_t* /* op */) {
  auto* ctx = static_cast<BlockOperationContext*>(cookie);
  ctx->status = status;
  sync_completion_signal(ctx->completion_event);
}

}  // namespace

namespace nand {

zx_status_t SynapticsBadBlock::Create(Config config, fbl::RefPtr<BadBlock>* out) {
  ddk::NandProtocolClient nand(&config.nand_proto);

  nand_info_t nand_info;
  size_t parent_op_size = 0;
  nand.Query(&nand_info, &parent_op_size);

  if (nand_info.oob_size < kOobSize) {
    zxlogf(ERROR, "%s: NAND supports only %u OOB bytes, at least %zu are needed", __func__,
           nand_info.oob_size, kOobSize);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ZX_DEBUG_ASSERT(nand_info.num_blocks % kEntriesPerByte == 0);
  ZX_DEBUG_ASSERT(nand_info.num_blocks / kEntriesPerByte <= nand_info.page_size);

  fbl::AllocChecker ac;
  fbl::Array<uint8_t> nand_op(new (&ac) uint8_t[parent_op_size], parent_op_size);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate memory for operation", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  zx::vmo data_vmo;
  zx_status_t status = zx::vmo::create(nand_info.page_size, 0, &data_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create VMO: %d", __func__, status);
    return status;
  }

  zx::vmo oob_vmo;
  if ((status = zx::vmo::create(nand_info.oob_size, 0, &oob_vmo)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create VMO: %d", __func__, status);
    return status;
  }

  *out = fbl::MakeRefCountedChecked<SynapticsBadBlock>(&ac, nand, config.bad_block_config,
                                                       nand_info, std::move(data_vmo),
                                                       std::move(oob_vmo), std::move(nand_op));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate memory for SynapticsBadBlock", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t SynapticsBadBlock::GetBadBlockList(uint32_t first_block, uint32_t last_block,
                                               fbl::Array<uint32_t>* bad_blocks) {
  fbl::AutoLock al(&lock_);

  if (!bbt_contents_) {
    zx_status_t status = ReadBadBlockTable();
    if (status != ZX_OK) {
      return status;
    }
  }

  // First loop to count the number of bad blocks, then create the array.

  size_t bad_block_count = 0;
  for (uint32_t i = first_block; i <= last_block; i++) {
    if (IsBadBlock(bbt_contents_, i)) {
      bad_block_count++;
    }
  }

  if (bad_block_count == 0) {
    bad_blocks->reset();
    return ZX_OK;
  }

  fbl::AllocChecker ac;
  bad_blocks->reset(new (&ac) uint32_t[bad_block_count], bad_block_count);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate memory for bad block array", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  size_t bad_block_index = 0;
  for (uint32_t i = first_block; i <= last_block; i++) {
    if (IsBadBlock(bbt_contents_, i)) {
      (*bad_blocks)[bad_block_index++] = i;
    }
  }

  return ZX_OK;
}

zx_status_t SynapticsBadBlock::MarkBlockBad(uint32_t block) {
  fbl::AutoLock al(&lock_);

  zx_status_t status;
  if (!bbt_contents_ && (status = ReadBadBlockTable()) != ZX_OK) {
    return status;
  }

  if (IsBadBlock(bbt_contents_, block)) {
    // Block is already marked bad.
    return ZX_OK;
  }

  SetBlockBad(&bbt_contents_, block);
  bbt_version_++;

  for (bool wrote_bbt = false;;) {
    if ((status = WriteBadBlockTableToVmo()) != ZX_OK) {
      return status;
    }

    status = WriteBadBlockTable(bbt_block_, InvalidBlock(), kTablePattern, &bbt_block_);
    if (status == ZX_ERR_IO) {
      SetBlockBad(&bbt_contents_, bbt_block_);

      // The bad block table version number can be reused if this first write fails repeatedly.
      if (wrote_bbt) {
        bbt_version_++;
      }

      wrote_bbt = false;
      continue;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: No good bad block table blocks left", __func__);
      return ZX_ERR_IO_DATA_LOSS;
    }

    wrote_bbt = true;

    status = WriteBadBlockTable(bbt_mirror_block_, bbt_block_, kMirrorPattern, &bbt_mirror_block_);
    if (status == ZX_ERR_IO) {
      SetBlockBad(&bbt_contents_, bbt_mirror_block_);
      bbt_version_++;
    } else {
      if (status != ZX_OK) {
        zxlogf(WARNING, "%s: Only one good bad block table block left", __func__);
      }

      break;
    }
  }

  return ZX_OK;
}

uint32_t SynapticsBadBlock::FindNextGoodTableBlock(uint32_t start_block, uint32_t except_block) {
  const uint32_t table_blocks =
      config_.synaptics.table_end_block - config_.synaptics.table_start_block + 1;

  // If start_block is valid start searching from start_block + 1, otherwise start searchin from the
  // beginning of the table.
  if (IsBlockValid(start_block)) {
    start_block++;
  } else {
    start_block = config_.synaptics.table_start_block;
  }

  for (uint32_t i = 0; i < table_blocks; i++) {
    const uint32_t block = ((start_block + i) % table_blocks) + config_.synaptics.table_start_block;
    if (block != except_block && !IsBadBlock(bbt_contents_, block)) {
      return block;
    }
  }

  return InvalidBlock();
}

zx_status_t SynapticsBadBlock::ReadBadBlockTablePattern(uint32_t block,
                                                        uint8_t out_pattern[kPatternSize],
                                                        uint8_t* out_version) {
  zx_status_t status = ReadFirstPage(block);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t oob_buffer[kPatternSize + sizeof(*out_version)];
  if ((status = oob_vmo_.read(oob_buffer, kTablePatternOffset, sizeof(oob_buffer))) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read VMO: %d", __func__, status);
    return status;
  }

  memcpy(out_pattern, oob_buffer, kPatternSize);
  *out_version = oob_buffer[kPatternSize];
  return status;
}

uint32_t SynapticsBadBlock::FindBadBlockTable() {
  uint32_t table_block = InvalidBlock();

  // Find the bad block table with the highest version number.
  for (uint32_t i = config_.synaptics.table_end_block; i >= config_.synaptics.table_start_block;
       i--) {
    uint8_t version;
    uint8_t pattern[kPatternSize];
    if (ReadBadBlockTablePattern(i, pattern, &version) == ZX_OK) {
      if (memcmp(pattern, kTablePattern, sizeof(pattern)) == 0) {
        bbt_block_ = i;
      } else if (memcmp(pattern, kMirrorPattern, sizeof(pattern)) == 0) {
        bbt_mirror_block_ = i;
      } else {
        continue;
      }

      if (version > bbt_version_ || !IsBlockValid(table_block)) {
        table_block = i;
        bbt_version_ = version;
      }
    }
  }

  return table_block;
}

zx_status_t SynapticsBadBlock::ReadBadBlockTable() {
  uint32_t table_block = FindBadBlockTable();
  if (!IsBlockValid(table_block)) {
    zxlogf(ERROR, "%s: No bad block table found", __func__);
    return ZX_ERR_NOT_FOUND;
  }

  const uint32_t bad_block_table_size = nand_info_.num_blocks / kEntriesPerByte;

  fbl::AllocChecker ac;
  bbt_contents_.reset(new (&ac) uint8_t[bad_block_table_size], bad_block_table_size);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate memory for bad block table", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ReadFirstPage(table_block);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = data_vmo_.read(bbt_contents_.data(), 0, bbt_contents_.size())) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read VMO: %d", __func__, status);
  }

  return status;
}

zx_status_t SynapticsBadBlock::WriteBadBlockTableToVmo() {
  zx_status_t status = data_vmo_.write(bbt_contents_.data(), 0, bbt_contents_.size());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write VMO: %d", __func__, status);
    return status;
  }

  if ((status = oob_vmo_.write(&bbt_version_, kTableVersionOffset, sizeof(bbt_version_))) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write VMO: %d", __func__, status);
  }

  return status;
}

zx_status_t SynapticsBadBlock::WriteBadBlockTable(uint32_t block, uint32_t except_block,
                                                  const uint8_t pattern[kPatternSize],
                                                  uint32_t* out_block) {
  zx_status_t status = oob_vmo_.write(pattern, kTablePatternOffset, kPatternSize);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write VMO: %d", __func__, status);
    return status;
  }

  *out_block = FindNextGoodTableBlock(block, except_block);

  while (IsBlockValid(*out_block)) {
    status = WriteFirstPage(*out_block);
    if (status == ZX_OK || status == ZX_ERR_IO) {
      return status;
    }

    *out_block = FindNextGoodTableBlock(*out_block, except_block);
  }

  return ZX_ERR_IO_DATA_LOSS;
}

zx_status_t SynapticsBadBlock::ReadFirstPage(uint32_t block) {
  sync_completion_t completion;
  BlockOperationContext op_ctx = {.completion_event = &completion, .status = ZX_ERR_INTERNAL};

  auto* nand_op = reinterpret_cast<nand_operation_t*>(nand_op_.data());
  nand_op->rw.command = NAND_OP_READ;
  nand_op->rw.data_vmo = data_vmo_.get();
  nand_op->rw.oob_vmo = oob_vmo_.get();
  nand_op->rw.length = 1;
  nand_op->rw.offset_nand = block * nand_info_.pages_per_block;
  nand_op->rw.offset_data_vmo = 0;
  nand_op->rw.offset_oob_vmo = 0;
  nand_op->rw.corrected_bit_flips = 0;

  nand_.Queue(nand_op, CompletionCallback, &op_ctx);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  if (op_ctx.status != ZX_OK) {
    zxlogf(ERROR, "%s: NAND read failed: %d", __func__, op_ctx.status);
  }

  return op_ctx.status;
}

zx_status_t SynapticsBadBlock::WriteFirstPage(uint32_t block) {
  sync_completion_t completion;
  BlockOperationContext op_ctx = {.completion_event = &completion, .status = ZX_ERR_INTERNAL};

  auto* nand_op = reinterpret_cast<nand_operation_t*>(nand_op_.data());

  nand_op->erase.command = NAND_OP_ERASE;
  nand_op->erase.first_block = block;
  nand_op->erase.num_blocks = 1;

  nand_.Queue(nand_op, CompletionCallback, &op_ctx);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  if (op_ctx.status != ZX_OK) {
    zxlogf(ERROR, "%s: NAND erase failed: %d", __func__, op_ctx.status);
    return op_ctx.status;
  }

  op_ctx.status = ZX_ERR_INTERNAL;

  nand_op->rw.command = NAND_OP_WRITE;
  nand_op->rw.data_vmo = data_vmo_.get();
  nand_op->rw.oob_vmo = oob_vmo_.get();
  nand_op->rw.length = 1;
  nand_op->rw.offset_nand = block * nand_info_.pages_per_block;
  nand_op->rw.offset_data_vmo = 0;
  nand_op->rw.offset_oob_vmo = 0;
  nand_op->rw.corrected_bit_flips = 0;

  nand_.Queue(nand_op, CompletionCallback, &op_ctx);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  if (op_ctx.status != ZX_OK) {
    zxlogf(ERROR, "%s: NAND write failed: %d", __func__, op_ctx.status);
  }

  return op_ctx.status;
}

}  // namespace nand
