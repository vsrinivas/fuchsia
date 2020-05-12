// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
#define SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_

#include <lib/operation/block.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <array>
#include <atomic>
#include <memory>

#include <ddk/trace/event.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <hw/sdmmc.h>

#include "sdmmc-device.h"

namespace sdmmc {

// See the eMMC specification section 7.4.69 for these constants.
enum EmmcPartition : uint8_t {
  USER_DATA_PARTITION = 0x0,
  BOOT_PARTITION_1 = 0x1,
  BOOT_PARTITION_2 = 0x2,
  PARTITION_COUNT,
};

struct PartitionInfo {
  enum EmmcPartition partition;
  uint64_t block_count;
};

using BlockOperation = block::BorrowedOperation<PartitionInfo>;

class SdmmcBlockDevice;
class PartitionDevice;

using PartitionDeviceType = ddk::Device<PartitionDevice, ddk::GetSizable, ddk::GetProtocolable>;

class PartitionDevice : public PartitionDeviceType,
                        public ddk::BlockImplProtocol<PartitionDevice, ddk::base_protocol>,
                        public ddk::BlockPartitionProtocol<PartitionDevice> {
 public:
  PartitionDevice(zx_device_t* parent, SdmmcBlockDevice* sdmmc_parent,
                  const block_info_t& block_info, EmmcPartition partition)
      : PartitionDeviceType(parent),
        sdmmc_parent_(sdmmc_parent),
        block_info_(block_info),
        partition_(partition) {}

  zx_status_t AddDevice();

  void DdkRelease() { delete this; }

  zx_off_t DdkGetSize();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* btxn, block_impl_queue_callback completion_cb, void* cookie);

  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

 private:
  SdmmcBlockDevice* const sdmmc_parent_;
  const block_info_t block_info_;
  const EmmcPartition partition_;
};

class SdmmcBlockDevice;
using SdmmcBlockDeviceType = ddk::Device<SdmmcBlockDevice, ddk::UnbindableNew>;

class SdmmcBlockDevice : public SdmmcBlockDeviceType {
 public:
  SdmmcBlockDevice(zx_device_t* parent, const SdmmcDevice& sdmmc)
      : SdmmcBlockDeviceType(parent), sdmmc_(sdmmc) {
    block_info_.max_transfer_size = static_cast<uint32_t>(sdmmc_.host_info().max_transfer_size);
  }
  ~SdmmcBlockDevice() { txn_list_.CompleteAll(ZX_ERR_INTERNAL); }

  static zx_status_t Create(zx_device_t* parent, const SdmmcDevice& sdmmc,
                            std::unique_ptr<SdmmcBlockDevice>* out_dev);

  zx_status_t ProbeSd();
  zx_status_t ProbeMmc();

  zx_status_t AddDevice() TA_EXCL(lock_);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }

  // Called by children of this device.
  void Queue(BlockOperation txn) TA_EXCL(lock_);

  // Visible for testing.
  zx_status_t Init() { return sdmmc_.Init(); }
  void StopWorkerThread() TA_EXCL(lock_);
  void SetBlockInfo(uint32_t block_size, uint64_t block_count);

 private:
  zx_status_t ReadWrite(const block_read_write_t& txn, const EmmcPartition partition) TA_REQ(lock_);
  zx_status_t Trim(const block_trim_t& txn, const EmmcPartition partition) TA_REQ(lock_);
  zx_status_t SetPartition(const EmmcPartition partition) TA_REQ(lock_);
  int WorkerThread();

  zx_status_t WaitForTran();

  zx_status_t MmcDoSwitch(uint8_t index, uint8_t value);
  zx_status_t MmcSetBusWidth(sdmmc_bus_width_t bus_width, uint8_t mmc_ext_csd_bus_width);
  sdmmc_bus_width_t MmcSelectBusWidth();
  zx_status_t MmcSwitchTiming(sdmmc_timing_t new_timing);
  zx_status_t MmcSwitchFreq(uint32_t new_freq);
  zx_status_t MmcDecodeExtCsd();
  bool MmcSupportsHs();
  bool MmcSupportsHsDdr();
  bool MmcSupportsHs200();
  bool MmcSupportsHs400();

  SdmmcDevice sdmmc_;

  sdmmc_bus_width_t bus_width_;
  sdmmc_timing_t timing_;

  uint32_t clock_rate_;  // Bus clock rate

  // mmc
  std::array<uint8_t, SDMMC_CID_SIZE> raw_cid_;
  std::array<uint8_t, SDMMC_CSD_SIZE> raw_csd_;
  std::array<uint8_t, MMC_EXT_CSD_SIZE> raw_ext_csd_;

  fbl::Mutex lock_;
  fbl::ConditionVariable worker_event_ TA_GUARDED(lock_);

  // blockio requests
  block::BorrowedOperationQueue<PartitionInfo> txn_list_ TA_GUARDED(lock_);

  // outstanding request (1 right now)
  sdmmc_req_t req_;

  thrd_t worker_thread_ = 0;

  std::atomic<bool> dead_ = false;

  block_info_t block_info_{};

  bool is_sd_ = false;

  EmmcPartition current_partition_ TA_GUARDED(lock_) = EmmcPartition::USER_DATA_PARTITION;
};

}  // namespace sdmmc

#endif  // SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
