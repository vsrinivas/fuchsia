// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_H_
#define SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_H_

#include <fuchsia/hardware/nand/c/fidl.h>
#include <inttypes.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <optional>

#include <ddk/metadata/nand.h>
#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/nand.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

// Wrapper for fuchsia_hardware_nand_Info. It simplifies initialization of NandDevice.
struct NandParams : public fuchsia_hardware_nand_Info {
  NandParams() : NandParams(0, 0, 0, 0, 0) {}

  NandParams(uint32_t page_size, uint32_t pages_per_block, uint32_t num_blocks, uint32_t ecc_bits,
             uint32_t oob_size)
      : NandParams(fuchsia_hardware_nand_Info{page_size,
                                              pages_per_block,
                                              num_blocks,
                                              ecc_bits,
                                              oob_size,
                                              fuchsia_hardware_nand_Class_FTL,
                                              {}}) {}

  NandParams(const fuchsia_hardware_nand_Info& base) {
    // NandParams has no data members.
    *this = *reinterpret_cast<const NandParams*>(&base);
  }

  uint64_t GetSize() const { return static_cast<uint64_t>(page_size + oob_size) * NumPages(); }

  uint32_t NumPages() const { return pages_per_block * num_blocks; }
};

class NandDevice;
using DeviceType = ddk::Device<NandDevice, ddk::GetSizable, ddk::Initializable, ddk::UnbindableNew,
                               ddk::Messageable>;

// Provides the bulk of the functionality for a ram-backed NAND device.
class NandDevice : public DeviceType, public ddk::NandProtocol<NandDevice, ddk::base_protocol> {
 public:
  explicit NandDevice(const NandParams& params, zx_device_t* parent = nullptr);
  ~NandDevice();

  zx_status_t Bind(const fuchsia_hardware_nand_RamNandInfo& info);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() { delete this; }

  // Performs the object initialization, returning the required data to create
  // an actual device (to call device_add()). The provided callback will be
  // called when this device must be removed from the system.
  zx_status_t Init(char name[NAME_MAX], zx::vmo vmo);

  // Device protocol implementation.
  zx_off_t DdkGetSize() { return params_.GetSize(); }
  void DdkUnbindNew(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  // Fidl RamNand implementation.
  zx_status_t Unlink();

  // NAND protocol implementation.
  void NandQuery(fuchsia_hardware_nand_Info* info_out, size_t* nand_op_size_out);
  void NandQueue(nand_operation_t* operation, nand_queue_callback completion_cb, void* cookie);
  zx_status_t NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                         size_t* num_bad_blocks);

 private:
  void Kill();
  bool AddToList(nand_operation_t* operation, nand_queue_callback completion_cb, void* cookie);
  bool RemoveFromList(nand_operation_t** operation);
  int WorkerThread();
  static int WorkerThreadStub(void* arg);
  uint32_t MainDataSize() const { return params_.NumPages() * params_.page_size; }

  // Implementation of the actual commands.
  zx_status_t ReadWriteData(nand_operation_t* operation);
  zx_status_t ReadWriteOob(nand_operation_t* operation);
  zx_status_t Erase(nand_operation_t* operation);

  uintptr_t mapped_addr_ = 0;
  zx::vmo vmo_;

  NandParams params_;

  fbl::Mutex lock_;
  list_node_t txn_list_ TA_GUARDED(lock_) = {};
  bool dead_ TA_GUARDED(lock_) = false;

  bool thread_created_ = false;

  sync_completion_t wake_signal_;
  thrd_t worker_;

  std::optional<nand_config_t> export_nand_config_;
  fbl::Array<char> export_partition_map_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};

#endif  // SRC_DEVICES_NAND_DRIVERS_RAM_NAND_RAM_NAND_H_
