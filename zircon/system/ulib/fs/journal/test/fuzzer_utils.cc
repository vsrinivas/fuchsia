// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "fuzzer_utils.h"

#include <lib/cksum.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/device/block.h>

#include <fs/journal/format.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <storage/buffer/blocking-ring-buffer.h>
#include <storage/operation/operation.h>
#include <storage/operation/unbuffered-operations-builder.h>

namespace fs {

using storage::BlockBuffer;
using storage::BlockingRingBuffer;
using storage::Operation;
using storage::OperationType;
using storage::UnbufferedOperation;
using storage::UnbufferedOperationsBuilder;
using storage::VmoBuffer;

// Most of the allocations below, whether for BlockBuffers or block_fifo_requests, are in terms of
// blocks. As such, the general strategy is to fill these allocations with between 1 and twice the
// expected number of bytes. This exercises both truncated data and out-of-bounds reads with
// non-zero data while also limiting the number of fuzzed bytes used for this purpose.

// FuzzedVmoidRegistry

zx_status_t FuzzedVmoidRegistry::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
  vmos_.emplace(std::make_pair(next_vmoid_, zx::unowned_vmo(vmo.get())));
  *out = next_vmoid_;
  next_vmoid_ =
      static_cast<vmoid_t>(ReservedVmoid::kMaxReserved) + static_cast<vmoid_t>(vmos_.size());
  return ZX_OK;
}

zx_status_t FuzzedVmoidRegistry::DetachVmo(vmoid_t vmoid) {
  vmos_.erase(vmoid);
  return ZX_OK;
}

// FuzzedTransactionHandler

void FuzzedTransactionHandler::Init(FuzzerUtils* fuzz_utils) {
  fuzz_utils_ = fuzz_utils;
  // Pick a block size between 2^9 (512) and 2^15 (32768)
  block_size_ = 1 << fuzz_utils_->data_provider()->ConsumeIntegralInRange<size_t>(9, 15);
}

zx_status_t FuzzedTransactionHandler::RunOperation(const Operation& operation,
                                                   BlockBuffer* buffer) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FuzzedTransactionHandler::Transaction(block_fifo_request_t* requests, size_t count) {
  if (fuzz_utils_->data_provider()->remaining_bytes() == 0) {
    return ZX_ERR_IO;
  }
  for (size_t i = 0; i < count; ++i) {
    const zx::vmo& vmo = fuzz_utils_->registry()->GetVmo(requests[i].vmoid);
    if (requests[i].opcode == BLOCKIO_READ && requests[i].vmo_offset == 0 &&
        requests[i].dev_offset == journal_start_ && requests[i].length == kJournalMetadataBlocks) {
      size_t info_len = sizeof(JournalInfo) * 2;
      auto info_bytes = fuzz_utils_->data_provider()->ConsumeBytes<uint8_t>(info_len);
      vmo.write(&info_bytes[0], 0, info_bytes.size());
    } else {
      size_t data_len = requests[i].length * block_size_;
      auto data_bytes = fuzz_utils_->data_provider()->ConsumeBytes<uint8_t>(data_len);
      vmo.write(&data_bytes[0], requests[i].vmo_offset, data_bytes.size());
    }
  }
  return ZX_OK;
}

// FuzzerUtils

zx_status_t FuzzerUtils::CreateRingBuffer(const char* label, ReservedVmoid vmoid, size_t len,
                                          std::unique_ptr<BlockingRingBuffer>* out) {
  registry_.SetNextVmoid(vmoid);
  return BlockingRingBuffer::Create(&registry_, len, block_size(), label, out);
}

zx_status_t FuzzerUtils::FuzzSuperblock(JournalSuperblock* out_info) {
  auto info_buffer = std::make_unique<VmoBuffer>();
  size_t info_blocks = input_.ConsumeIntegralInRange<size_t>(1, kJournalMetadataBlocks * 2);
  registry_.SetNextVmoid(ReservedVmoid::kInfoVmoid);
  zx_status_t status =
      info_buffer->Initialize(&registry_, info_blocks, block_size(), "fuzzed-info");
  if (status != ZX_OK) {
    return status;
  }
  // Create a JournalInfo with a valid magic and checksum to pass shallow checks.
  JournalInfo info;
  info.magic = kJournalMagic;
  info.start_block = input_.ConsumeIntegral<uint64_t>();
  info.reserved = input_.ConsumeIntegral<uint64_t>();
  info.timestamp = input_.ConsumeIntegral<uint64_t>();
  info.checksum = 0;
  info.checksum = crc32(0, reinterpret_cast<const uint8_t*>(&info), sizeof(JournalInfo));
  memcpy(info_buffer->Data(0), &info, sizeof(JournalInfo));
  std::unique_ptr<BlockBuffer> fuzzed_info(info_buffer.release());
  *out_info = JournalSuperblock(std::move(fuzzed_info));
  return ZX_OK;
}

zx_status_t FuzzerUtils::FuzzJournal(VmoBuffer* out_journal) {
  auto journal_blocks = input_.ConsumeIntegralInRange<size_t>(1, kEntryMetadataBlocks * 2);
  registry_.SetNextVmoid(ReservedVmoid::kJournalVmoid);
  zx_status_t status =
      out_journal->Initialize(&registry_, journal_blocks, block_size(), "fuzzed-journal");
  if (status != ZX_OK) {
    return status;
  }
  auto journal_bytes = input_.ConsumeBytes<uint8_t>(out_journal->capacity());
  memcpy(out_journal->Data(0), journal_bytes.data(), journal_bytes.size());
  return ZX_OK;
}

fbl::Vector<UnbufferedOperation> FuzzerUtils::FuzzOperation(ReservedVmoid vmoid) {
  UnbufferedOperationsBuilder builder;
  if (registry_.HasVmo(vmoid)) {
    const zx::vmo& vmo = registry_.GetVmo(vmoid);
    builder.Add({
        zx::unowned_vmo(vmo.get()),
        {
            .type = input_.ConsumeEnum<OperationType>(),
            .vmo_offset = input_.ConsumeIntegral<uint64_t>(),
            .dev_offset = input_.ConsumeIntegral<uint64_t>(),
            .length = input_.ConsumeIntegral<uint64_t>(),
        },
    });
  }
  return builder.TakeOperations();
}

}  // namespace fs
