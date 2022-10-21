// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDHCI_SDHCI_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDHCI_SDHCI_H_

#include <fuchsia/hardware/sdhci/cpp/banjo.h>
#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <lib/ddk/io-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/sdmmc/hw.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/threads.h>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "sdhci-reg.h"
#include "src/lib/vmo_store/vmo_store.h"

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

    uint64_t get_address() const {
      uint64_t addr;
      memcpy(&addr, &address, sizeof(addr));
      return addr;
    }
  } __PACKED;
  static_assert(sizeof(AdmaDescriptor96) == 12, "unexpected ADMA2 descriptor size");

  struct AdmaDescriptor64 {
    uint16_t attr;
    uint16_t length;
    uint32_t address;
  } __PACKED;
  static_assert(sizeof(AdmaDescriptor64) == 8, "unexpected ADMA2 descriptor size");

  Sdhci(zx_device_t* parent, fdf::MmioBuffer regs_mmio_buffer, zx::bti bti, zx::interrupt irq,
        const ddk::SdhciProtocolClient sdhci, uint64_t quirks, uint64_t dma_boundary_alignment)
      : DeviceType(parent),
        regs_mmio_buffer_(std::move(regs_mmio_buffer)),
        irq_(std::move(irq)),
        sdhci_(sdhci),
        bti_(std::move(bti)),
        quirks_(quirks),
        dma_boundary_alignment_(dma_boundary_alignment),
        registered_vmo_stores_{
            // SdmmcVmoStore does not have a default constructor, so construct each one using an
            // empty Options (do not map or pin automatically upon VMO registration).
            // clang-format off
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            SdmmcVmoStore{vmo_store::Options{}},
            // clang-format on
        } {}

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
  zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb)
      TA_EXCL(mtx_);
  void SdmmcAckInBandInterrupt() TA_EXCL(mtx_);
  zx_status_t SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo, uint64_t offset,
                               uint64_t size, uint32_t vmo_rights);
  zx_status_t SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo);
  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]) TA_EXCL(mtx_);

  // Visible for testing.
  zx_status_t Init();

  uint32_t base_clock() const { return base_clock_; }

 protected:
  // All protected members are visible for testing.
  enum class RequestStatus {
    IDLE,
    COMMAND,
    TRANSFER_DATA_DMA,
    READ_DATA_PIO,
    WRITE_DATA_PIO,
    BUSY_RESPONSE,
  };

  RequestStatus GetRequestStatus() TA_EXCL(&mtx_) {
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
    if (pending_request_.is_pending()) {
      const bool has_data = pending_request_.cmd_flags & SDMMC_RESP_DATA_PRESENT;
      const bool busy_response = pending_request_.cmd_flags & SDMMC_RESP_LEN_48B;

      if (!pending_request_.cmd_done) {
        return RequestStatus::COMMAND;
      }
      if (has_data) {
        return RequestStatus::TRANSFER_DATA_DMA;
      }
      if (busy_response) {
        return RequestStatus::BUSY_RESPONSE;
      }
    }
    return RequestStatus::IDLE;
  }

  virtual zx_status_t WaitForReset(SoftwareReset mask);
  virtual zx_status_t WaitForInterrupt() { return irq_.wait(nullptr); }

  fdf::MmioBuffer regs_mmio_buffer_;

  // DMA descriptors, visible for testing
  ddk::IoBuffer iobuf_ = {};

 private:
  // TODO(fxbug.dev/106851): Move these back to sdhci.cc after SdmmcRequest has been removed.
  static constexpr uint32_t Hi32(zx_paddr_t val) {
    return static_cast<uint32_t>((val >> 32) & 0xffffffff);
  }
  static constexpr uint32_t Lo32(zx_paddr_t val) { return val & 0xffffffff; }

  // for 2M max transfer size for fully discontiguous
  // also see SDMMC_PAGES_COUNT in fuchsia/hardware/sdmmc/c/banjo.h
  static constexpr int kDmaDescCount = 512;

  // 64k max per descriptor
  static constexpr size_t kMaxDescriptorLength = 0x1'0000;

  struct OwnedVmoInfo {
    uint64_t offset;
    uint64_t size;
    uint32_t rights;
  };

  using SdmmcVmoStore = vmo_store::VmoStore<vmo_store::HashTableStorage<uint32_t, OwnedVmoInfo>>;

  // Maintains a list of physical memory regions to be used for DMA. Input buffers are pinned if
  // needed, and unpinned upon destruction.
  class DmaDescriptorBuilder {
   public:
    DmaDescriptorBuilder(const sdmmc_req_new_t& request, SdmmcVmoStore& registered_vmos,
                         uint64_t dma_boundary_alignment, zx::unowned_bti bti)
        : request_(request),
          registered_vmos_(registered_vmos),
          dma_boundary_alignment_(dma_boundary_alignment),
          bti_(std::move(bti)) {}
    ~DmaDescriptorBuilder() {
      for (size_t i = 0; i < pmt_count_; i++) {
        pmts_[i].unpin();
      }
    }

    // Appends the physical memory regions for this buffer to the end of the list.
    zx_status_t ProcessBuffer(const sdmmc_buffer_region_t& buffer);

    // Builds DMA descriptors of the template type in the array provided.
    template <typename DescriptorType>
    zx_status_t BuildDmaDescriptors(cpp20::span<DescriptorType> out_descriptors);

    size_t block_count() const {
      ZX_DEBUG_ASSERT(request_.blocksize != 0);
      return total_size_ / request_.blocksize;
    }
    size_t descriptor_count() const { return descriptor_count_; }

   private:
    // Pins the buffer if needed, and fills out_regions with the physical addresses corresponding to
    // the (owned or unowned) input buffer. Contiguous runs of pages are condensed into single
    // regions so that the minimum number of DMA descriptors are required.
    zx::result<size_t> GetPinnedRegions(uint32_t vmo_id, const sdmmc_buffer_region_t& buffer,
                                        cpp20::span<fzl::PinnedVmo::Region> out_regions);
    zx::result<size_t> GetPinnedRegions(zx::unowned_vmo vmo, const sdmmc_buffer_region_t& buffer,
                                        cpp20::span<fzl::PinnedVmo::Region> out_regions);

    // Appends the regions to the current list of regions being tracked by this object. Regions are
    // split if needed according to hardware restrictions on size or alignment.
    zx_status_t AppendRegions(cpp20::span<const fzl::PinnedVmo::Region> regions);

    const sdmmc_req_new_t& request_;
    SdmmcVmoStore& registered_vmos_;
    const uint64_t dma_boundary_alignment_;
    std::array<fzl::PinnedVmo::Region, SDMMC_PAGES_COUNT> regions_ = {};
    size_t region_count_ = 0;
    size_t total_size_ = 0;
    size_t descriptor_count_ = 0;
    std::array<zx::pmt, SDMMC_PAGES_COUNT> pmts_ = {};
    size_t pmt_count_ = 0;
    const zx::unowned_bti bti_;
  };

  static void PrepareCmd(sdmmc_req_t* req, TransferMode* transfer_mode, Command* command);
  static void PrepareCmd(const sdmmc_req_new_t& req, TransferMode* transfer_mode,
                         Command* command) {
    sdmmc_req_t old_req{.cmd_idx = req.cmd_idx, .cmd_flags = req.cmd_flags};
    PrepareCmd(&old_req, transfer_mode, command);
  }

  bool SupportsAdma2() const {
    return (info_.caps & SDMMC_HOST_CAP_DMA) && !(quirks_ & SDHCI_QUIRK_NO_DMA);
  }

  void EnableInterrupts() TA_REQ(mtx_);
  void DisableInterrupts() TA_REQ(mtx_);

  zx_status_t WaitForInhibit(const PresentState mask) const;
  zx_status_t WaitForInternalClockStable() const;

  void CompleteRequestLocked(sdmmc_req_t* req, zx_status_t status) TA_REQ(mtx_);
  void CmdStageCompleteLocked() TA_REQ(mtx_);
  void DataStageReadReadyLocked() TA_REQ(mtx_);
  void DataStageWriteReadyLocked() TA_REQ(mtx_);
  void TransferCompleteLocked() TA_REQ(mtx_);
  void ErrorRecoveryLocked() TA_REQ(mtx_);

  int IrqThread() TA_EXCL(mtx_);

  zx_status_t PinRequestPages(sdmmc_req_t* req, zx_paddr_t* phys, size_t pagecount);

  template <typename DescriptorType>
  zx_status_t BuildDmaDescriptor(sdmmc_req_t* req, DescriptorType* descs);
  zx_status_t StartRequestLocked(sdmmc_req_t* req) TA_REQ(mtx_);
  zx_status_t FinishRequest(sdmmc_req_t* req);

  zx_status_t SgStartRequest(const sdmmc_req_new_t& request, DmaDescriptorBuilder& builder)
      TA_REQ(mtx_);
  zx_status_t SetUpDma(const sdmmc_req_new_t& request, DmaDescriptorBuilder& builder) TA_REQ(mtx_);
  zx_status_t SgFinishRequest(const sdmmc_req_new_t& request, uint32_t out_response[4])
      TA_REQ(mtx_);

  void SgHandleInterrupt(InterruptStatus status) TA_REQ(mtx_);
  void SgCmdStageComplete() TA_REQ(mtx_);
  void SgTransferComplete() TA_REQ(mtx_);
  void SgDataStageReadReady() TA_REQ(mtx_);
  void SgErrorRecovery() TA_REQ(mtx_);
  void SgCompleteRequest(zx_status_t status) TA_REQ(mtx_);

  zx::interrupt irq_;
  thrd_t irq_thread_;

  const ddk::SdhciProtocolClient sdhci_;

  zx::bti bti_;

  // Held when a command or action is in progress.
  fbl::Mutex mtx_;

  // These are used to synchronize the request thread(s) with the interrupt thread, for requests
  // from SdmmcRequest (see PendingRequest for SdmmcRequestNew). To be removed after all requests
  // use SdmmcRequestNew.
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
  bool card_interrupt_masked_ TA_GUARDED(mtx_) = false;

  // Keep one SdmmcVmoStore for each possible client ID (IDs are in [0, SDMMC_MAX_CLIENT_ID]).
  std::array<SdmmcVmoStore, SDMMC_MAX_CLIENT_ID + 1> registered_vmo_stores_;

  // Used to synchronize the request thread(s) with the interrupt thread for requests through
  // SdmmcRequestNew. See above for SdmmcRequest requests.
  struct PendingRequest {
    PendingRequest() { Reset(); }

    // Initializes the PendingRequest based on the command index and flags. cmd_done is set to false
    // to indicate that there is now a request pending.
    void Init(const sdmmc_req_new_t& request) {
      cmd_idx = request.cmd_idx;
      cmd_flags = request.cmd_flags;
      cmd_done = false;
      // No data phase if there is no data present and no busy response.
      data_done = !(cmd_flags & (SDMMC_RESP_DATA_PRESENT | SDMMC_RESP_LEN_48B));
    }

    uint32_t cmd_idx;
    // If false, a command is in progress on the bus, and the interrupt thread is waiting for the
    // command complete interrupt.
    bool cmd_done;
    // If false, data is being transferred on the bus, and the interrupt thread is waiting for the
    // transfer complete interrupt. Set to true for requests that have no data transfer.
    bool data_done;
    // The flags for the current request, used to determine what response (if any) is expected from
    // this command.
    uint32_t cmd_flags;
    // The 0-, 32-, or 128-bit response (unused fields set to zero). Set by the interrupt thread and
    // read by the request thread.
    uint32_t response[4];
    // The final status of the request. Set by the interrupt thread and read by the request thread.
    zx_status_t status;

    bool is_pending() const { return !cmd_done || !data_done; }

    void Reset() {
      cmd_done = true;
      data_done = true;
      cmd_idx = 0;
      cmd_flags = 0;
      memset(response, 0, sizeof(response));
      status = ZX_ERR_IO;
    }
  };

  PendingRequest pending_request_ TA_GUARDED(mtx_);
};

}  // namespace sdhci

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDHCI_SDHCI_H_
