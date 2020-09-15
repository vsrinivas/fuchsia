// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_driver.h"

#include <lib/ftl/ndm-driver.h>
#include <zircon/assert.h>

#include <memory>
#include <new>

#include <ddk/debug.h>
#include <ddktl/protocol/badblock.h>
#include <fbl/array.h>

#include "ddk/protocol/nand.h"
#include "nand_operation.h"
#include "oob_doubler.h"

namespace ftl {
extern thread_local int g_nand_op_count;
}

namespace {

void CountNestedOperation() { ftl::g_nand_op_count++; }

uint32_t GetParameter(const char* key) {
  const char* value = getenv(key);
  if (!value) {
    return 0;
  }
  return static_cast<uint32_t>(strtoul(value, nullptr, 0));
}

__PRINTFLIKE(1, 2) void LogTrace(const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(TRACE, format, args);
  va_end(args);
}

__PRINTFLIKE(1, 2) void LogDebug(const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(DEBUG, format, args);
  va_end(args);
}

__PRINTFLIKE(1, 2) void LogInfo(const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(INFO, format, args);
  va_end(args);
}

__PRINTFLIKE(1, 2) void LogWarning(const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(WARNING, format, args);
  va_end(args);
}

__PRINTFLIKE(1, 2) void LogError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  zxlogvf(ERROR, format, args);
  va_end(args);
}

class NandDriverImpl final : public ftl::NandDriver {
 public:
  NandDriverImpl(const nand_protocol_t* parent, const bad_block_protocol_t* bad_block)
      : parent_(parent), bad_block_protocol_(bad_block) {}
  ~NandDriverImpl() final {}

  // NdmDriver interface:
  const char* Init() final;
  const char* Attach(const ftl::Volume* ftl_volume) final;
  bool Detach() final;
  int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer) final;
  int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                const void* oob_buffer) final;
  int NandErase(uint32_t page_num) final;
  int IsBadBlock(uint32_t page_num) final;
  bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) final;
  const fuchsia_hardware_nand_Info& info() const final { return info_; }

 private:
  // Returns true if initialization was performed with an alternate configuration.
  // |options| is passed by value, so a temporary object will be created by the
  // compiler.
  bool HandleAlternateConfig(const ftl::Volume* ftl_volume, ftl::VolumeOptions options);
  bool GetBadBlocks();

  ftl::OobDoubler parent_;
  size_t op_size_ = 0;
  fuchsia_hardware_nand_Info info_ = {};
  const bad_block_protocol_t* bad_block_protocol_;
  fbl::Array<uint32_t> bad_blocks_;
};

const char* NandDriverImpl::Init() {
  parent_.Query(&info_, &op_size_);
  zxlogf(INFO, "FTL: Nand: page_size %u, block size %u, %u blocks, %u ecc, %u oob, op size %lu",
         info_.page_size, info_.pages_per_block, info_.num_blocks, info_.ecc_bits, info_.oob_size,
         op_size_);

  if (!GetBadBlocks()) {
    return "Failed to query bad blocks";
  }

  ZX_DEBUG_ASSERT(info_.oob_size >= 16);
  return nullptr;
}

const char* NandDriverImpl::Attach(const ftl::Volume* ftl_volume) {
  ftl::VolumeOptions options = {
      .num_blocks = info_.num_blocks,
      // This should be 2%, but that is of the whole device, not just this partition.
      // TODO(fxbug.dev/39372): This value should be provided by the stack. For now, use 2% for
      // small disks (likely tests).
      .max_bad_blocks = info_.num_blocks > 1000 ? 41 : info_.num_blocks / 50,
      .block_size = info_.page_size * info_.pages_per_block,
      .page_size = info_.page_size,
      .eb_size = info_.oob_size,
      // If flags change, make sure that HandleAlternateConfig() still makes sense.
      .flags = ftl::kReadOnlyInit};

  if (!IsNdmDataPresent(options)) {
    if (HandleAlternateConfig(ftl_volume, options)) {
      // Already handled.
      return nullptr;
    }
    options.flags = 0;
  } else if (BadBbtReservation()) {
    return "Unable to use bad block reservation";
  }
  ftl::LoggerProxy logger = {
      .trace = &LogTrace,
      .debug = &LogDebug,
      .info = &LogInfo,
      .warn = &LogWarning,
      .error = &LogError,
  };
  const char* error = CreateNdmVolumeWithLogger(ftl_volume, options, true, logger);
  if (error) {
    // Retry allowing the volume to be fixed as needed.
    zxlogf(INFO, "FTL: About to retry volume creation");
    options.flags = 0;
    error = CreateNdmVolume(ftl_volume, options);
  }

  if (!error && !volume_data_saved()) {
    // Initialization is complete; update the control data format, but ignore errors.
    if (!WriteVolumeData()) {
      zxlogf(ERROR, "FTL: Failed to upgrade NDM version");
    }
  }
  return error;
}

bool NandDriverImpl::Detach() { return RemoveNdmVolume(); }

// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int NandDriverImpl::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                             void* oob_buffer) {
  CountNestedOperation();
  ftl::NandOperation operation(op_size_);
  uint32_t data_pages = page_buffer ? page_count : 0;
  size_t data_size = data_pages * info_.page_size;
  size_t oob_size = (oob_buffer ? page_count : 0) * info_.oob_size;
  size_t num_bytes = data_size + oob_size;

  nand_operation_t* op = operation.GetOperation();
  op->rw.command = NAND_OP_READ;
  op->rw.offset_nand = start_page;
  op->rw.length = page_count;

  zx_status_t status = ZX_OK;
  if (page_buffer) {
    status = operation.SetDataVmo(num_bytes);
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: SetDataVmo Failed: %d", status);
      return ftl::kNdmFatalError;
    }
  }

  if (oob_buffer) {
    status = operation.SetOobVmo(num_bytes);
    op->rw.offset_oob_vmo = data_pages;
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: SetOobVmo Failed: %d", status);
      return ftl::kNdmFatalError;
    }
  }

  zxlogf(TRACE, "FTL: Read page, start %d, len %d", start_page, page_count);
  status = operation.Execute(&parent_);
  if (status == ZX_ERR_IO_DATA_INTEGRITY) {
    return ftl::kNdmUncorrectableEcc;
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "FTL: Read failed: %d", status);
    return ftl::kNdmFatalError;
  }

  if (page_buffer) {
    memcpy(page_buffer, operation.buffer(), data_size);
  }

  if (oob_buffer) {
    memcpy(oob_buffer, operation.buffer() + data_size, oob_size);
  }

  // This threshold is somewhat arbitrary, and should be adjusted if we deal
  // with multiple controllers (by making it part of the nand protocol), or
  // if we find it inappropriate after running endurance tests. We could also
  // decide we need the FTL to have a more active role detecting blocks that
  // should be moved around.
  if (op->rw.corrected_bit_flips > info_.ecc_bits / 2) {
    return ftl::kNdmUnsafeEcc;
  }

  return ftl::kNdmOk;
}

// Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking the block as bad.
int NandDriverImpl::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                              const void* oob_buffer) {
  CountNestedOperation();
  ftl::NandOperation operation(op_size_);
  uint32_t data_pages = page_buffer ? page_count : 0;
  size_t data_size = data_pages * info_.page_size;
  size_t oob_size = (oob_buffer ? page_count : 0) * info_.oob_size;
  size_t num_bytes = data_size + oob_size;

  nand_operation_t* op = operation.GetOperation();
  op->rw.command = NAND_OP_WRITE;
  op->rw.offset_nand = start_page;
  op->rw.length = page_count;

  zx_status_t status = ZX_OK;
  if (page_buffer) {
    status = operation.SetDataVmo(num_bytes);
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: SetDataVmo Failed: %d", status);
      return ftl::kNdmFatalError;
    }
    memcpy(operation.buffer(), page_buffer, data_size);
  }

  if (oob_buffer) {
    status = operation.SetOobVmo(num_bytes);
    op->rw.offset_oob_vmo = data_pages;
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTL: SetOobVmo Failed: %d", status);
      return ftl::kNdmFatalError;
    }
    memcpy(operation.buffer() + data_size, oob_buffer, oob_size);
  }

  zxlogf(TRACE, "FTL: Write page, start %d, len %d", start_page, page_count);
  status = operation.Execute(&parent_);
  if (status != ZX_OK) {
    return status == ZX_ERR_IO ? ftl::kNdmError : ftl::kNdmFatalError;
  }

  return ftl::kNdmOk;
}

// Returns kNdmOk or kNdmError. kNdmError triggers marking the block as bad.
int NandDriverImpl::NandErase(uint32_t page_num) {
  CountNestedOperation();
  uint32_t block_num = page_num / info_.pages_per_block;
  ftl::NandOperation operation(op_size_);

  nand_operation_t* op = operation.GetOperation();
  op->erase.command = NAND_OP_ERASE;
  op->erase.first_block = block_num;
  op->erase.num_blocks = 1;

  zxlogf(TRACE, "FTL: Erase block num %d", block_num);

  zx_status_t status = operation.Execute(&parent_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTL: NandErase failed: %d", status);
    return status == ZX_ERR_IO ? ftl::kNdmError : ftl::kNdmFatalError;
  }

  return ftl::kNdmOk;
}

// Returns kTrue, kFalse or kNdmError.
int NandDriverImpl::IsBadBlock(uint32_t page_num) {
  if (!bad_blocks_.size()) {
    return ftl::kFalse;
  }

  // The list should be really short.
  uint32_t block_num = page_num / info_.pages_per_block;
  for (uint32_t bad_block : bad_blocks_) {
    if (bad_block == block_num) {
      zxlogf(ERROR, "FTL: IsBadBlock(%d) found", block_num);
      return ftl::kTrue;
    }
  }
  return ftl::kFalse;
}

bool NandDriverImpl::IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) {
  return IsEmptyPageImpl(data, info_.page_size, spare, info_.oob_size);
}

bool NandDriverImpl::HandleAlternateConfig(const ftl::Volume* ftl_volume,
                                           ftl::VolumeOptions options) {
  uint32_t num_blocks = GetParameter("driver.ftl.original-size");
  if (!num_blocks || num_blocks >= info_.num_blocks) {
    return false;
  }
  options.num_blocks = num_blocks;

  if (!IsNdmDataPresent(options)) {
    // Nothing at the alternate location.
    return false;
  }
  RemoveNdmVolume();

  options.flags = 0;  // Allow automatic fixing of errors.
  zxlogf(INFO, "FTL: About to read volume of size %u blocks", num_blocks);
  if (!IsNdmDataPresent(options)) {
    zxlogf(ERROR, "FTL: Failed to read initial volume");
    return true;
  }

  if (!SaveBadBlockData()) {
    zxlogf(ERROR, "FTL: Failed to extract bad block table");
    return true;
  }
  RemoveNdmVolume();

  ftl::NandOperation operation(op_size_);
  operation.GetOperation()->command = NAND_OP_ERASE;
  // Erase all new blocks when extending.
  operation.GetOperation()->erase.first_block = num_blocks;
  operation.GetOperation()->erase.num_blocks = info_.num_blocks - num_blocks;
  if (operation.Execute(&parent_) != ZX_OK) {
    return true;
  }

  options.num_blocks = info_.num_blocks;
  if (!IsNdmDataPresent(options)) {
    zxlogf(ERROR, "FTL: Failed to NDM extend volume");
    return true;
  }
  if (!RestoreBadBlockData()) {
    zxlogf(ERROR, "FTL: Failed to write bad block table");
    return true;
  }

  const char* error = CreateNdmVolume(ftl_volume, options);
  if (error) {
    zxlogf(ERROR, "FTL: Failed to extend volume: %s", error);
  } else {
    zxlogf(INFO, "FTL: Volume successfully extended");
  }

  return true;
}

bool NandDriverImpl::GetBadBlocks() {
  if (!bad_block_protocol_->ops) {
    return true;
  }
  ddk::BadBlockProtocolClient client(const_cast<bad_block_protocol_t*>(bad_block_protocol_));

  size_t num_bad_blocks;
  zx_status_t status = client.GetBadBlockList(nullptr, 0, &num_bad_blocks);
  if (status != ZX_OK) {
    return false;
  }
  if (!num_bad_blocks) {
    return true;
  }

  std::unique_ptr<uint32_t[]> bad_block_list(new uint32_t[num_bad_blocks]);
  size_t new_count;
  status = client.GetBadBlockList(bad_block_list.get(), num_bad_blocks, &new_count);
  if (status != ZX_OK) {
    return false;
  }
  ZX_ASSERT(new_count == num_bad_blocks);

  bad_blocks_ = fbl::Array<uint32_t>(bad_block_list.release(), num_bad_blocks);

  for (uint32_t bad_block : bad_blocks_) {
    zxlogf(ERROR, "FTL: Bad block: %x", bad_block);
  }
  return true;
}

}  // namespace

namespace ftl {

// Static.
std::unique_ptr<NandDriver> NandDriver::Create(const nand_protocol_t* parent,
                                               const bad_block_protocol_t* bad_block) {
  return std::unique_ptr<NandDriver>(new NandDriverImpl(parent, bad_block));
}

}  // namespace ftl.
