// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_MTK_SDMMC_MTK_SDMMC_H_
#define SRC_DEVICES_BLOCK_DRIVERS_MTK_SDMMC_MTK_SDMMC_H_

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/phys-iter.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <utility>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <soc/mt8167/mt8167-sdmmc.h>

#include "mtk-sdmmc-reg.h"

namespace sdmmc {

constexpr uint32_t kPageMask = PAGE_SIZE - 1;

struct RequestStatus {
  RequestStatus() : cmd_status(ZX_OK), data_status(ZX_OK) {}

  RequestStatus(zx_status_t status) : cmd_status(status), data_status(ZX_OK) {}

  RequestStatus(zx_status_t cmd, zx_status_t data) : cmd_status(cmd), data_status(data) {}

  zx_status_t Get() const { return cmd_status == ZX_OK ? data_status : cmd_status; }

  zx_status_t cmd_status;
  zx_status_t data_status;
};

class TuneWindow;

class MtkSdmmc;
using DeviceType = ddk::Device<MtkSdmmc>;

class MtkSdmmc : public DeviceType, public ddk::SdmmcProtocol<MtkSdmmc, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  virtual ~MtkSdmmc() = default;

  void DdkRelease();

  zx_status_t Bind();

  zx_status_t SdmmcHostInfo(sdmmc_host_info_t* info);
  zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
  zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
  zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
  zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
  void SdmmcHwReset();
  zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
  zx_status_t SdmmcRequest(sdmmc_req_t* req);
  zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb);
  zx_status_t SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo, uint64_t offset,
                               uint64_t size, uint32_t vmo_rights);
  zx_status_t SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo);
  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]);

  // Visible for testing.
  MtkSdmmc(zx_device_t* parent, ddk::MmioBuffer mmio, zx::bti bti, const sdmmc_host_info_t& info,
           zx::interrupt irq, const ddk::GpioProtocolClient& reset_gpio,
           const ddk::GpioProtocolClient& power_en_gpio, const board_mt8167::MtkSdmmcConfig& config)
      : DeviceType(parent),
        req_(nullptr),
        mmio_(std::move(mmio)),
        bti_(std::move(bti)),
        info_(info),
        irq_(std::move(irq)),
        cmd_status_(ZX_OK),
        reset_gpio_(reset_gpio),
        power_en_gpio_(power_en_gpio),
        config_(config) {}

  // Visible for testing.
  zx_status_t Init();

  // Visible for testing.
 protected:
  virtual zx_status_t WaitForInterrupt(zx::time* timestamp);

  int JoinIrqThread() { return thrd_join(irq_thread_, nullptr); }

  fbl::Mutex mutex_;
  sdmmc_req_t* req_ TA_GUARDED(mutex_);

 private:
  RequestStatus SdmmcRequestWithStatus(sdmmc_req_t* req);

  // Prepares the VMO and the DMA engine for receiving data.
  zx_status_t RequestPrepareDma(sdmmc_req_t* req) TA_REQ(mutex_);
  // Creates the GPDMA and BDMA descriptors.
  zx_status_t SetupDmaDescriptors(phys_iter_buffer_t* phys_iter_buf);
  // Waits for the DMA engine to finish and unpins the VMO pages.
  zx_status_t RequestFinishDma(sdmmc_req_t* req) TA_REQ(mutex_);

  // Clears the FIFO in preparation for receiving data.
  zx_status_t RequestPreparePolled(sdmmc_req_t* req) TA_REQ(mutex_);
  // Polls the FIFO register for received data.
  zx_status_t RequestFinishPolled(sdmmc_req_t* req) TA_REQ(mutex_);

  RequestStatus SendTuningBlock(uint32_t cmd_idx, zx_handle_t vmo);

  // Iterates over the possible delay values to find the optimal window. set_delay is a function
  // that accepts and applies a uint32_t delay value, and do_request is a function that sends the
  // request and returns its status. The test results are saved in window.
  template <typename DelayCallback, typename RequestCallback>
  void TestDelaySettings(DelayCallback&& set_delay, RequestCallback&& do_request,
                         TuneWindow* window);

  int IrqThread();

  // Finish the command portion of the request. Returns true if control should be passed back to
  // the main thread or false if more interrupts are expected. This should be called from the IRQ
  // thread with mutex_ held.
  bool CmdDone(const MsdcInt& msdc_int) TA_REQ(mutex_);

  ddk::MmioBuffer mmio_;
  zx::bti bti_;
  const sdmmc_host_info_t info_;
  zx::interrupt irq_;
  thrd_t irq_thread_;
  io_buffer_t gpdma_buf_;
  io_buffer_t bdma_buf_;
  sync_completion_t req_completion_;
  zx_status_t cmd_status_ TA_GUARDED(mutex_);
  const ddk::GpioProtocolClient reset_gpio_;
  const ddk::GpioProtocolClient power_en_gpio_;
  const board_mt8167::MtkSdmmcConfig config_;
  ddk::InBandInterruptProtocolClient interrupt_cb_;
};

// TuneWindow keeps track of the results of a series of tuning tests. It is expected that either
// Pass or Fail is called after each test, and that each subsequent delay value is greater than the
// one before it. The largest window of passing tests is determined as the tests are run, and at the
// end the optimal delay value is chosen as the middle of the largest window.
class TuneWindow {
 public:
  TuneWindow() : index_(0), best_start_(0), best_size_(0), current_start_(0), current_size_(0) {}

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

#endif  // SRC_DEVICES_BLOCK_DRIVERS_MTK_SDMMC_MTK_SDMMC_H_
