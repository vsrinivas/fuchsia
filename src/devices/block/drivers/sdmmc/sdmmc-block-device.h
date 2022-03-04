// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <lib/ddk/trace/event.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/operation/block.h>
#include <lib/sdmmc/hw.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/types.h>

#include <array>
#include <atomic>
#include <cinttypes>
#include <deque>
#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>

#include "sdmmc-device.h"
#include "sdmmc-types.h"

namespace sdmmc {

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
  void DdkRelease() {
    StopWorkerThread();
    delete this;
  }

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
  zx_status_t MmcWaitForSwitch(uint8_t index, uint8_t value);
  zx_status_t MmcSetBusWidth(sdmmc_bus_width_t bus_width, uint8_t mmc_ext_csd_bus_width);
  sdmmc_bus_width_t MmcSelectBusWidth();
  // The host is expected to switch the timing from HS200 to HS as part of HS400 initialization.
  // Checking the status of the switch requires special handling to avoid a temporary mismatch
  // between the host and device timings.
  zx_status_t MmcSwitchTiming(sdmmc_timing_t new_timing);
  zx_status_t MmcSwitchTimingHs200ToHs();
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
  inspect::UintProperty io_errors_;   // Only updated from the worker thread.
  inspect::UintProperty io_retries_;  // Only updated from the worker thread.
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_BLOCK_DEVICE_H_
