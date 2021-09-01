// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fuchsia/hardware/badblock/c/banjo.h>
#include <fuchsia/hardware/badblock/cpp/banjo.h>
#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <fuchsia/hardware/nand/c/banjo.h>
#include <lib/ftl/volume.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "src/devices/block/drivers//ftl/metrics.h"
#include "src/devices/block/drivers/ftl/nand_driver.h"

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
using DeviceType = ddk::Device<BlockDevice, ddk::GetSizable, ddk::Unbindable,
                               ddk::Messageable<fuchsia_hardware_block::Ftl>::Mixin,
                               ddk::Suspendable, ddk::Resumable, ddk::GetProtocolable>;

// Exposes the FTL library as a Fuchsia BlockDevice protocol.
class BlockDevice : public DeviceType,
                    public ddk::BlockImplProtocol<BlockDevice, ddk::base_protocol>,
                    public ddk::BlockPartitionProtocol<BlockDevice>,
                    public ftl::FtlInstance {
 public:
  explicit BlockDevice(zx_device_t* parent = nullptr) : DeviceType(parent) {}
  ~BlockDevice();

  zx_status_t Bind();
  void DdkRelease() { delete this; }
  void DdkUnbind(ddk::UnbindTxn txn);

  // Performs the object initialization.
  zx_status_t Init();

  // Device protocol implementation.
  zx_off_t DdkGetSize() { return params_.GetSize(); }
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t Suspend();
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkResume(ddk::ResumeTxn txn) {
    txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
  }
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

  // Block protocol implementation.
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb, void* cookie);

  // Partition protocol implementation.
  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

  void Format(FormatRequestView request, FormatCompleter::Sync& completer) final {
    completer.Reply(FormatInternal());
  }

  void GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) final {
    completer.ReplySuccess(DuplicateInspectVmo());
  }

  // FtlInstance interface.
  bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) final;

  // Issues a command to format the FTL (aka, delete all data).
  zx_status_t FormatInternal();

  // Returns a read_only handle to the underlying Inspect VMO.
  zx::vmo DuplicateInspectVmo() const { return metrics_.DuplicateInspectVmo(); }

  OperationCounters& nand_counters() { return nand_counters_; }

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
  list_node_t txn_list_ TA_GUARDED(lock_) = LIST_INITIAL_VALUE(txn_list_);
  bool dead_ TA_GUARDED(lock_) = false;

  bool thread_created_ = false;
  bool pending_flush_ = false;

  sync_completion_t wake_signal_;
  thrd_t worker_;

  nand_protocol_t parent_ = {};
  bad_block_protocol_t bad_block_ = {};

  std::unique_ptr<ftl::Volume> volume_;

  uint8_t guid_[ZBI_PARTITION_GUID_LEN] = {};

  Metrics metrics_;

  // Keeps track of the nand operations being issued for each incoming block operation.
  OperationCounters nand_counters_;
};

}  // namespace ftl

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FTL_BLOCK_DEVICE_H_
