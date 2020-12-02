// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_H_
#define SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_H_

#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/threads.h>

#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/sdhci.h>
#include <ddktl/protocol/sdmmc.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <hw/sdmmc.h>

#include "sdhci-reg.h"

namespace sdhci {

class Sdhci;
using DeviceType = ddk::Device<Sdhci, ddk::Unbindable>;

class Sdhci : public DeviceType, public ddk::SdmmcProtocol<Sdhci, ddk::base_protocol> {
 public:
  // Visible for testing.
  struct AdmaDescriptor96 {
    uint16_t attr;
    uint16_t length;
    uint64_t address;
  } __PACKED;
  static_assert(sizeof(AdmaDescriptor96) == 12, "unexpected ADMA2 descriptor size");

  struct AdmaDescriptor64 {
    uint16_t attr;
    uint16_t length;
    uint32_t address;
  } __PACKED;
  static_assert(sizeof(AdmaDescriptor64) == 8, "unexpected ADMA2 descriptor size");

  Sdhci(zx_device_t* parent, ddk::MmioBuffer regs_mmio_buffer, zx::bti bti, zx::interrupt irq,
        const ddk::SdhciProtocolClient sdhci, uint64_t quirks, uint64_t dma_boundary_alignment)
      : DeviceType(parent),
        regs_mmio_buffer_(std::move(regs_mmio_buffer)),
        irq_(std::move(irq)),
        sdhci_(sdhci),
        bti_(std::move(bti)),
        quirks_(quirks),
        dma_boundary_alignment_(dma_boundary_alignment) {}

  virtual ~Sdhci() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info);
  zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) TA_EXCL(mtx_);
  zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) TA_EXCL(mtx_);
  zx_status_t SdmmcSetBusFreq(uint32_t bus_freq) TA_EXCL(mtx_);
  zx_status_t SdmmcSetTiming(sdmmc_timing_t timing) TA_EXCL(mtx_);
  void SdmmcHwReset() TA_EXCL(mtx_);
  zx_status_t SdmmcPerformTuning(uint32_t cmd_idx) TA_EXCL(mtx_);
  zx_status_t SdmmcRequest(sdmmc_req_t* req) TA_EXCL(mtx_);
  zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb);
  zx_status_t SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo, uint64_t offset,
                               uint64_t size, uint32_t vmo_rights);
  zx_status_t SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo);
  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]);

  // Visible for testing.
  zx_status_t Init();

  uint32_t base_clock() const { return base_clock_; }

 protected:
  // Visible for testing.
  enum class RequestStatus {
    IDLE,
    COMMAND,
    TRANSFER_DATA_DMA,
    READ_DATA_PIO,
    WRITE_DATA_PIO,
    BUSY_RESPONSE,
  };

  virtual zx_status_t WaitForReset(const SoftwareReset mask);
  virtual zx_status_t WaitForInterrupt() { return irq_.wait(nullptr); }
  virtual zx_status_t PinRequestPages(sdmmc_req_t* req, zx_paddr_t* phys, size_t pagecount);

  RequestStatus GetRequestStatus() TA_EXCL(mtx_) {
    fbl::AutoLock lock(&mtx_);
    if (cmd_req_ != nullptr) {
      return RequestStatus::COMMAND;
    }
    if (data_req_ != nullptr) {
      const bool has_data = data_req_->cmd_flags & SDMMC_RESP_DATA_PRESENT;
      const bool busy_response = data_req_->cmd_flags & SDMMC_RESP_LEN_48B;

      if (has_data) {
        if (data_req_->use_dma) {
          return RequestStatus::TRANSFER_DATA_DMA;
        }
        if (data_req_->cmd_flags & SDMMC_CMD_READ) {
          return RequestStatus::READ_DATA_PIO;
        }
        return RequestStatus::WRITE_DATA_PIO;
      }
      if (busy_response) {
        return RequestStatus::BUSY_RESPONSE;
      }
    }
    return RequestStatus::IDLE;
  }

  ddk::MmioBuffer regs_mmio_buffer_;

  // DMA descriptors, visible for testing
  ddk::IoBuffer iobuf_ = {};

 private:
  static void PrepareCmd(sdmmc_req_t* req, TransferMode* transfer_mode, Command* command);

  bool SupportsAdma2() const {
    return (info_.caps & SDMMC_HOST_CAP_DMA) && !(quirks_ & SDHCI_QUIRK_NO_DMA);
  }

  zx_status_t WaitForInhibit(const PresentState mask) const;
  zx_status_t WaitForInternalClockStable() const;

  void CompleteRequestLocked(sdmmc_req_t* req, zx_status_t status) TA_REQ(mtx_);
  void CmdStageCompleteLocked() TA_REQ(mtx_);
  void DataStageReadReadyLocked() TA_REQ(mtx_);
  void DataStageWriteReadyLocked() TA_REQ(mtx_);
  void TransferCompleteLocked() TA_REQ(mtx_);
  void ErrorRecoveryLocked() TA_REQ(mtx_);

  int IrqThread() TA_EXCL(mtx_);

  template <typename DescriptorType>
  zx_status_t BuildDmaDescriptor(sdmmc_req_t* req, DescriptorType* descs);
  zx_status_t StartRequestLocked(sdmmc_req_t* req) TA_REQ(mtx_);
  zx_status_t FinishRequest(sdmmc_req_t* req);

  zx::interrupt irq_;
  thrd_t irq_thread_;

  const ddk::SdhciProtocolClient sdhci_;

  zx::bti bti_;

  // Held when a command or action is in progress.
  fbl::Mutex mtx_;

  // Current command request
  sdmmc_req_t* cmd_req_ TA_GUARDED(mtx_) = nullptr;
  // Current data line request
  sdmmc_req_t* data_req_ TA_GUARDED(mtx_) = nullptr;
  // Current block id to transfer (PIO)
  uint16_t data_blockid_ TA_GUARDED(mtx_) = 0;
  // Set to true if the data stage completed before the command stage
  bool data_done_ TA_GUARDED(mtx_) = false;
  // used to signal request complete
  sync_completion_t req_completion_;

  // Controller info
  sdmmc_host_info_t info_ = {};

  // Controller specific quirks
  const uint64_t quirks_;
  const uint64_t dma_boundary_alignment_;

  // Base clock rate
  uint32_t base_clock_ = 0;

  ddk::InBandInterruptProtocolClient interrupt_cb_;
};

}  // namespace sdhci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_SDHCI_SDHCI_H_
