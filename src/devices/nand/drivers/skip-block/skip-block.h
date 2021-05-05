// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_
#define SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_

#include <fuchsia/hardware/badblock/cpp/banjo.h>
#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/operation/nand.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "logical-to-physical-map.h"

namespace nand {

using NandOperation = nand::Operation<>;

using fuchsia_hardware_skipblock::wire::PartitionInfo;
using fuchsia_hardware_skipblock::wire::ReadWriteOperation;
using fuchsia_hardware_skipblock::wire::WriteBytesMode;
using fuchsia_hardware_skipblock::wire::WriteBytesOperation;

struct PageRange {
  size_t page_offset;
  size_t page_count;
};

class SkipBlockDevice;
using DeviceType = ddk::Device<SkipBlockDevice, ddk::GetSizable, ddk::Unbindable, ddk::Messageable>;

class SkipBlockDevice : public DeviceType,
                        public fidl::WireServer<fuchsia_hardware_skipblock::SkipBlock>,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_SKIP_BLOCK> {
 public:
  // Spawns device node based on parent node.
  static zx_status_t Create(void*, zx_device_t* parent);

  zx_status_t Bind();

  // Device protocol implementation.
  zx_off_t DdkGetSize();
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // skip-block fidl implementation.
  void GetPartitionInfo(GetPartitionInfoRequestView request,
                        GetPartitionInfoCompleter::Sync& completer) override;
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;
  void WriteBytes(WriteBytesRequestView request, WriteBytesCompleter::Sync& completer) override;
  void WriteBytesWithoutErase(WriteBytesWithoutEraseRequestView request,
                              WriteBytesWithoutEraseCompleter::Sync& completer) override;

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
  zx_status_t WriteLocked(ReadWriteOperation op, bool* bad_block_grown,
                          std::optional<PageRange> write_page_range) TA_REQ(lock_);
  zx_status_t WriteBytesWithoutEraseLocked(size_t page_offset, size_t page_count,
                                           ReadWriteOperation op) TA_REQ(lock_);

  zx_status_t ReadPartialBlocksLocked(WriteBytesOperation op, uint64_t block_size,
                                      uint64_t first_block, uint64_t last_block, uint64_t op_size,
                                      zx::vmo* vmo) TA_REQ(lock_);

  ddk::NandProtocolClient nand_ __TA_GUARDED(lock_);
  ddk::BadBlockProtocolClient bad_block_ __TA_GUARDED(lock_);
  LogicalToPhysicalMap block_map_ __TA_GUARDED(lock_);
  fbl::Mutex lock_;
  nand_info_t nand_info_;
  size_t parent_op_size_;

  std::optional<NandOperation> nand_op_ __TA_GUARDED(lock_);

  const uint32_t copy_count_;
};

}  // namespace nand

#endif  // SRC_DEVICES_NAND_DRIVERS_SKIP_BLOCK_SKIP_BLOCK_H_
