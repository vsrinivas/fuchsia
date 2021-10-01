// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_

#include <fidl/fuchsia.hardware.rpmb/cpp/wire.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <fuchsia/hardware/rpmb/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/trace/event.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/operation/block.h>
#include <lib/sdmmc/hw.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <array>
#include <atomic>
#include <deque>
#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>

#include "sdmmc-device.h"

namespace sdmmc {

// See the eMMC specification section 7.4.69 for these constants.
enum EmmcPartition : uint8_t {
  USER_DATA_PARTITION = 0x0,
  BOOT_PARTITION_1 = 0x1,
  BOOT_PARTITION_2 = 0x2,
  RPMB_PARTITION = 0x3,
  PARTITION_COUNT,
};

struct PartitionInfo {
  enum EmmcPartition partition;
  uint64_t block_count;
};

struct RpmbRequestInfo {
  fuchsia_mem::wire::Range tx_frames = {};
  fuchsia_mem::wire::Range rx_frames = {};
  fidl::WireServer<fuchsia_hardware_rpmb::Rpmb>::RequestCompleter::Async completer;
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

class RpmbDevice;
using RpmbDeviceType =
    ddk::Device<RpmbDevice, ddk::Messageable<fuchsia_hardware_rpmb::Rpmb>::Mixin>;

class RpmbDevice : public RpmbDeviceType, public ddk::RpmbProtocol<RpmbDevice, ddk::base_protocol> {
 public:
  // sdmmc_parent is owned by the SDMMC root device when the RpmbDevice object is created. Ownership
  // is transferred to devmgr shortly after, meaning it will outlive this object due to the
  // parent/child device relationship.
  RpmbDevice(zx_device_t* parent, SdmmcBlockDevice* sdmmc_parent,
             const std::array<uint8_t, SDMMC_CID_SIZE>& cid,
             const std::array<uint8_t, MMC_EXT_CSD_SIZE>& ext_csd)
      : RpmbDeviceType(parent),
        sdmmc_parent_(sdmmc_parent),
        cid_(cid),
        rpmb_size_(ext_csd[MMC_EXT_CSD_RPMB_SIZE_MULT]),
        reliable_write_sector_count_(ext_csd[MMC_EXT_CSD_REL_WR_SEC_C]),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void DdkRelease() { delete this; }

  void RpmbConnectServer(zx::channel server);

  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;
  void Request(RequestRequestView request, RequestCompleter::Sync& completer) override;

 private:
  SdmmcBlockDevice* const sdmmc_parent_;
  const std::array<uint8_t, SDMMC_CID_SIZE> cid_;
  const uint8_t rpmb_size_;
  const uint8_t reliable_write_sector_count_;
  async::Loop loop_;
  bool loop_started_ = false;
};

class SdmmcBlockDevice;
using SdmmcBlockDeviceType = ddk::Device<SdmmcBlockDevice, ddk::Unbindable, ddk::Suspendable>;

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

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkRelease() { delete this; }

  // Called by children of this device.
  void Queue(BlockOperation txn) TA_EXCL(lock_);
  void RpmbQueue(RpmbRequestInfo info) TA_EXCL(lock_);

  // Visible for testing.
  zx_status_t Init() { return sdmmc_.Init(); }
  void StopWorkerThread() TA_EXCL(lock_);
  void SetBlockInfo(uint32_t block_size, uint64_t block_count);

 private:
  // An arbitrary limit to prevent RPMB clients from flooding us with requests.
  static constexpr size_t kMaxOutstandingRpmbRequests = 16;

  // The worker thread will handle this many block ops then this many RPMB requests, and will repeat
  // until both queues are empty.
  static constexpr size_t kRoundRobinRequestCount = 16;

  zx_status_t ReadWrite(const block_read_write_t& txn, const EmmcPartition partition);
  zx_status_t Trim(const block_trim_t& txn, const EmmcPartition partition);
  zx_status_t SetPartition(const EmmcPartition partition);
  zx_status_t RpmbRequest(const RpmbRequestInfo& request);

  void HandleBlockOps(block::BorrowedOperationQueue<PartitionInfo>& txn_list);
  void HandleRpmbRequests(std::deque<RpmbRequestInfo>& rpmb_list);

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

  SdmmcDevice sdmmc_;  // Only accessed by ProbeSd, ProbeMmc, and WorkerThread.

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
  std::deque<RpmbRequestInfo> rpmb_list_ TA_GUARDED(lock_);

  // outstanding request (1 right now)
  sdmmc_req_t req_;

  thrd_t worker_thread_ = 0;

  std::atomic<bool> dead_ = false;

  block_info_t block_info_{};

  bool is_sd_ = false;

  inspect::Inspector inspector_;
  inspect::Node root_;
  inspect::UintProperty io_errors_;  // Only updated from the worker thread.
  inspect::UintProperty io_retries_;  // Only updated from the worker thread.
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
