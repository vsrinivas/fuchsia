// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/sdmmc.h>

namespace sdmmc {

struct RequestStatus {
    RequestStatus()
        : cmd_status(ZX_OK), data_status(ZX_OK) {}

    RequestStatus(zx_status_t status)
        : cmd_status(status), data_status(ZX_OK) {}

    zx_status_t Get() const {
        return cmd_status == ZX_OK ? data_status : cmd_status;
    }

    zx_status_t cmd_status;
    zx_status_t data_status;
};

class TuneWindow;

class MtkSdmmc;
using DeviceType = ddk::Device<MtkSdmmc>;

class MtkSdmmc : public DeviceType, public ddk::SdmmcProtocol<MtkSdmmc> {

public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t SdmmcHostInfo(sdmmc_host_info_t* info);
    zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
    zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
    zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
    zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
    void SdmmcHwReset();
    zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
    zx_status_t SdmmcRequest(sdmmc_req_t* req);

private:
    MtkSdmmc(zx_device_t* parent, ddk::MmioBuffer mmio, zx::bti bti, const sdmmc_host_info_t& info)
        : DeviceType(parent), mmio_(fbl::move(mmio)), bti_(fbl::move(bti)), info_(info) {}

    void Init();

    RequestStatus SdmmcRequestWithStatus(sdmmc_req_t* req);

    // Prepares the VMO and the DMA engine for receiving data.
    RequestStatus RequestPrepareDma(sdmmc_req_t* req);
    // Waits for the DMA engine to finish and unpins the VMO pages.
    RequestStatus RequestFinishDma(sdmmc_req_t* req);

    // Clears the FIFO in preparation for receiving data.
    RequestStatus RequestPreparePolled(sdmmc_req_t* req);
    // Polls the FIFO register for received data.
    RequestStatus RequestFinishPolled(sdmmc_req_t* req);

    RequestStatus SendTuningBlock(uint32_t cmd_idx, zx_handle_t vmo);

    // Iterates over the possible delay values to find the optimal window. set_delay is a function
    // that accepts and applies a uint32_t delay value, and do_request is a function that sends the
    // request and returns its status. The test results are saved in window.
    template <typename DelayCallback, typename RequestCallback>
    void TestDelaySettings(DelayCallback&& set_delay, RequestCallback&& do_request,
                           TuneWindow* window);

    ddk::MmioBuffer mmio_;
    zx::bti bti_;
    const sdmmc_host_info_t info_;
    zx::pmt pmt_;
};

// TuneWindow keeps track of the results of a series of tuning tests. It is expected that either
// Pass or Fail is called after each test, and that each subsequent delay value is greater than the
// one before it. The largest window of passing tests is determined as the tests are run, and at the
// end the optimal delay value is chosen as the middle of the largest window.
class TuneWindow {
public:
    TuneWindow()
        : index_(0), best_start_(0), best_size_(0), current_start_(0), current_size_(0) {}

    // The tuning test passed, update the current window size and the best window size if needed.
    void Pass() {
        current_size_++;

        if (best_start_ == current_start_) {
            best_size_ = current_size_;
        }

        if (current_size_ > best_size_) {
            best_start_ = current_start_;
            best_size_ = current_size_;
        }

        index_++;
    }

    // The tuning test failed, update the best window size if needed.
    void Fail() {
        current_start_ = index_ + 1;
        current_size_ = 0;

        index_++;
    }

    // Returns the best window size and sets result to the best delay value. If the window size is
    // zero then no tuning tests passed.
    uint32_t GetDelay(uint32_t* delay) const {
        if (best_size_ != 0) {
            *delay = best_start_ + (best_size_ / 2);
        }

        return best_size_;
    }

private:
    uint32_t index_;
    uint32_t best_start_;
    uint32_t best_size_;
    uint32_t current_start_;
    uint32_t current_size_;
};

}  // namespace sdmmc
