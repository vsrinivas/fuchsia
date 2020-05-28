// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_
#define SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_

#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/operation/nand.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/protocol/badblock.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/nand.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "logical-to-physical-map.h"

namespace nand {

using NandOperation = nand::Operation<>;

using ::llcpp::fuchsia::hardware::skipblock::PartitionInfo;
using ::llcpp::fuchsia::hardware::skipblock::ReadWriteOperation;
using ::llcpp::fuchsia::hardware::skipblock::WriteBytesOperation;

class SkipBlockDevice;
using DeviceType =
    ddk::Device<SkipBlockDevice, ddk::GetSizable, ddk::UnbindableNew, ddk::Messageable>;

class SkipBlockDevice : public DeviceType,
                        public ::llcpp::fuchsia::hardware::skipblock::SkipBlock::Interface,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_SKIP_BLOCK> {
 public:
  // Spawns device node based on parent node.
  static zx_status_t Create(void*, zx_device_t* parent);

  zx_status_t Bind();

  // Device protocol implementation.
  zx_off_t DdkGetSize();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // skip-block fidl implementation.
  void GetPartitionInfo(GetPartitionInfoCompleter::Sync completer);
  void Read(ReadWriteOperation op, ReadCompleter::Sync completer);
  void Write(ReadWriteOperation op, WriteCompleter::Sync completer);
  void WriteBytes(WriteBytesOperation op, WriteBytesCompleter::Sync completer);
  void WriteBytesWithoutErase(WriteBytesOperation op,
                              WriteBytesWithoutEraseCompleter::Sync completer);

 private:
  explicit SkipBlockDevice(zx_device_t* parent, ddk::NandProtocolClient nand,
                           ddk::BadBlockProtocolClient bad_block, uint32_t copy_count)
      : DeviceType(parent), nand_(nand), bad_block_(bad_block), copy_count_(copy_count) {
    nand_.Query(&nand_info_, &parent_op_size_);
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(SkipBlockDevice);

  uint64_t GetBlockSize() const { return nand_info_.pages_per_block * nand_info_.page_size; }
  uint32_t GetBlockCountLocked() const TA_REQ(lock_);

  // Helper to get bad block list in a more idiomatic container.
  zx_status_t GetBadBlockList(fbl::Array<uint32_t>* bad_block_list) TA_REQ(lock_);
  // Helper to validate operation.
  zx_status_t ValidateOperationLocked(const ReadWriteOperation& op) const TA_REQ(lock_);
  zx_status_t ValidateOperationLocked(const WriteBytesOperation& op) const TA_REQ(lock_);

  zx_status_t ReadLocked(ReadWriteOperation op) TA_REQ(lock_);
  zx_status_t WriteLocked(ReadWriteOperation op, bool* bad_block_grown) TA_REQ(lock_);
  zx_status_t WriteBytesWithoutEraseLocked(size_t page_offset, size_t page_count,
                                           ReadWriteOperation op) TA_REQ(lock_);

  zx_status_t ReadPartialBlocksLocked(WriteBytesOperation op, uint64_t block_size,
                                      uint64_t first_block, uint64_t last_block, uint64_t op_size,
                                      zx::vmo* vmo) TA_REQ(lock_);

  ddk::NandProtocolClient nand_ __TA_GUARDED(lock_);
  ddk::BadBlockProtocolClient bad_block_ __TA_GUARDED(lock_);
  LogicalToPhysicalMap block_map_ __TA_GUARDED(lock_);
  fbl::Mutex lock_;
  fuchsia_hardware_nand_Info nand_info_;
  size_t parent_op_size_;

  std::optional<NandOperation> nand_op_ __TA_GUARDED(lock_);

  const uint32_t copy_count_;
};

}  // namespace nand

#endif  // SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_
