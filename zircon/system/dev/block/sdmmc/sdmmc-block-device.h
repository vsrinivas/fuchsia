// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <atomic>

#include <ddk/trace/event.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/operation/block.h>
#include <zircon/thread_annotations.h>

#include "sdmmc-device.h"

namespace sdmmc {

class SdmmcBlockDevice;
using SdmmcBlockDeviceType = ddk::Device<SdmmcBlockDevice, ddk::GetSizable, ddk::Unbindable>;

class SdmmcBlockDevice : public SdmmcBlockDeviceType,
                         public ddk::BlockImplProtocol<SdmmcBlockDevice, ddk::base_protocol>,
                         public fbl::RefCounted<SdmmcBlockDevice> {
public:
    SdmmcBlockDevice(zx_device_t* parent, const SdmmcDevice& sdmmc)
        : SdmmcBlockDeviceType(parent), sdmmc_(sdmmc) {
        block_info_.max_transfer_size = static_cast<uint32_t>(sdmmc_.host_info().max_transfer_size);
    }

    static zx_status_t Create(zx_device_t* parent, const SdmmcDevice& sdmmc,
                              fbl::RefPtr<SdmmcBlockDevice>* out_dev);

    zx_status_t ProbeSd();
    zx_status_t ProbeMmc();

    zx_status_t AddDevice();

    void DdkUnbind();
    void DdkRelease();

    zx_off_t DdkGetSize();

    void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
    void BlockImplQueue(block_op_t* btxn, block_impl_queue_callback completion_cb, void* cookie);

private:
    using BlockOperation = block::UnownedOperation<>;

    void BlockComplete(BlockOperation* txn, zx_status_t status, trace_async_id_t async_id);
    void DoTxn(BlockOperation* txn);
    int WorkerThread();

    zx_status_t WaitForTran();

    zx_status_t MmcDoSwitch(uint8_t index, uint8_t value);
    zx_status_t MmcSetBusWidth(sdmmc_bus_width_t bus_width, uint8_t mmc_ext_csd_bus_width);
    sdmmc_bus_width_t MmcSelectBusWidth();
    zx_status_t MmcSwitchTiming(sdmmc_timing_t new_timing);
    zx_status_t MmcSwitchFreq(uint32_t new_freq);
    zx_status_t MmcDecodeExtCsd(const uint8_t* raw_ext_csd);
    bool MmcSupportsHs();
    bool MmcSupportsHsDdr();
    bool MmcSupportsHs200();
    bool MmcSupportsHs400();

    std::atomic<trace_async_id_t> async_id_;

    SdmmcDevice sdmmc_;

    sdmmc_bus_width_t bus_width_;
    sdmmc_timing_t timing_;

    uint32_t clock_rate_;  // Bus clock rate

    // mmc
    uint32_t raw_cid_[4];
    uint32_t raw_csd_[4];
    uint8_t raw_ext_csd_[512];

    fbl::Mutex lock_;
    fbl::ConditionVariable worker_event_ TA_GUARDED(lock_);

    // blockio requests
    block::UnownedOperationQueue<> txn_list_ TA_GUARDED(lock_);

    // outstanding request (1 right now)
    sdmmc_req_t req_;

    thrd_t worker_thread_ = 0;

    std::atomic<bool> dead_ = false;

    block_info_t block_info_;

    bool is_sd_ = false;
};

}  // namespace sdmmc
