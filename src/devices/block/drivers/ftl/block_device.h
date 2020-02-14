// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_
#define SRC_STORAGE_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_

#include <lib/ftl/volume.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/protocol/badblock.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/badblock.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

namespace ftl {

struct BlockParams {
  uint64_t GetSize() const { return static_cast<uint64_t>(page_size) * num_pages; }

  uint32_t page_size;
  uint32_t num_pages;
};

// Ftl version of block_op_t.
// TODO(rvargas): Explore using c++ lists.
struct FtlOp {
  block_op_t op;
  list_node_t node;
  block_impl_queue_callback completion_cb;
  void* cookie;
};

class BlockDevice;
using DeviceType = ddk::Device<BlockDevice, ddk::GetSizable, ddk::UnbindableNew, ddk::Messageable,
                               ddk::SuspendableNew, ddk::ResumableNew, ddk::GetProtocolable>;

// Provides the bulk of the functionality for a FTL-backed block device.
class BlockDevice : public DeviceType,
                    public ddk::BlockImplProtocol<BlockDevice, ddk::base_protocol>,
                    public ddk::BlockPartitionProtocol<BlockDevice>,
                    public ftl::FtlInstance {
 public:
  explicit BlockDevice(zx_device_t* parent = nullptr) : DeviceType(parent) {}
  ~BlockDevice();

  zx_status_t Bind();
  void DdkRelease() { delete this; }
  void DdkUnbindNew(ddk::UnbindTxn txn);

  // Performs the object initialization.
  zx_status_t Init();

  // Device protocol implementation.
  zx_off_t DdkGetSize() { return params_.GetSize(); }
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t Suspend();
  void DdkSuspendNew(ddk::SuspendTxn txn);
  void DdkResumeNew(ddk::ResumeTxn txn) {
    txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
  }
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

  // Block protocol implementation.
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb, void* cookie);

  // Partition protocol implementation.
  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

  // FtlInstance interface.
  bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) final;

  // Issues a command to format the FTL (aka, delete all data).
  zx_status_t Format();

  void SetVolumeForTest(std::unique_ptr<ftl::Volume> volume) { volume_ = std::move(volume); }

  void SetNandParentForTest(const nand_protocol_t& nand) { parent_ = nand; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(BlockDevice);

 private:
  bool InitFtl();
  void Kill();
  bool AddToList(FtlOp* operation);
  bool RemoveFromList(FtlOp** operation);
  int WorkerThread();
  static int WorkerThreadStub(void* arg);

  // Implementation of the actual commands.
  zx_status_t ReadWriteData(block_op_t* operation);
  zx_status_t TrimData(block_op_t* operation);
  zx_status_t Flush();

  BlockParams params_ = {};

  fbl::Mutex lock_;
  list_node_t txn_list_ TA_GUARDED(lock_) = {};
  bool dead_ TA_GUARDED(lock_) = false;

  bool thread_created_ = false;
  bool pending_flush_ = false;

  sync_completion_t wake_signal_;
  thrd_t worker_;

  nand_protocol_t parent_ = {};
  bad_block_protocol_t bad_block_ = {};

  std::unique_ptr<ftl::Volume> volume_;
  uint8_t guid_[ZBI_PARTITION_GUID_LEN] = {};
};

}  // namespace ftl

#endif  // SRC_STORAGE_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_
