// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_DEVICE_H_

#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <lib/sdmmc/hw.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/time.h>

#include <array>

namespace sdmmc {

// SdmmcDevice wraps a ddk::SdmmcProtocolClient to provide helper methods to the SD/MMC and SDIO
// core drivers. It is assumed that the underlying SDMMC protocol driver can handle calls from
// different threads, although care should be taken when calling methods that update the RCA
// (SdSendRelativeAddr and MmcSetRelativeAddr) or change the signal voltage (SdSwitchUhsVoltage).
// These are typically not used outside the probe thread however, so generally no synchronization is
// required.
class SdmmcDevice {
 public:
  explicit SdmmcDevice(const ddk::SdmmcProtocolClient& host) : host_(host), host_info_({}) {}

  zx_status_t Init();

  const ddk::SdmmcProtocolClient& host() const { return host_; }
  const sdmmc_host_info_t& host_info() const { return host_info_; }

  bool UseDma() const { return host_info_.caps & SDMMC_HOST_CAP_DMA; }

  // Update the current voltage field, e.g. after reading the card status registers.
  void SetCurrentVoltage(sdmmc_voltage_t new_voltage) { signal_voltage_ = new_voltage; }

  void SetRequestRetries(uint32_t retries) { retries_ = retries; }

  // SD/MMC shared ops
  zx_status_t SdmmcGoIdle();
  zx_status_t SdmmcSendStatus(uint32_t* response);
  zx_status_t SdmmcStopTransmission(uint32_t* status = nullptr);
  zx_status_t SdmmcWaitForState(uint32_t state);
  // Retries a block read/write request. STOP_TRANSMISSION is issued after every attempt that
  // results in an error, but not after the request succeeds.
  zx_status_t SdmmcIoRequestWithRetries(sdmmc_req_t* request, uint32_t* retries);

  // SD ops
  zx_status_t SdSendOpCond(uint32_t flags, uint32_t* ocr);
  zx_status_t SdSendIfCond();
  zx_status_t SdSelectCard();
  zx_status_t SdSendScr(std::array<uint8_t, 8>& scr);
  zx_status_t SdSetBusWidth(sdmmc_bus_width_t width);

  // SD/SDIO shared ops
  zx_status_t SdSwitchUhsVoltage(uint32_t ocr);
  zx_status_t SdSendRelativeAddr(uint16_t* card_status);

  // SDIO ops
  zx_status_t SdioSendOpCond(uint32_t ocr, uint32_t* rocr);
  zx_status_t SdioIoRwDirect(bool write, uint32_t fn_idx, uint32_t reg_addr, uint8_t write_byte,
                             uint8_t* read_byte);
  zx_status_t SdioIoRwExtended(uint32_t caps, bool write, uint32_t fn_idx, uint32_t reg_addr,
                               bool incr, uint32_t blk_count, uint32_t blk_size, bool use_dma,
                               uint8_t* buf, zx_handle_t dma_vmo, uint64_t buf_offset);
  zx_status_t SdioIoRwExtended(uint32_t caps, bool write, uint8_t fn_idx, uint32_t reg_addr,
                               bool incr, uint32_t blk_count, uint32_t blk_size,
                               cpp20::span<const sdmmc_buffer_region_t> buffers);

  // MMC ops
  zx_status_t MmcSendOpCond(uint32_t ocr, uint32_t* rocr);
  zx_status_t MmcAllSendCid(std::array<uint8_t, SDMMC_CID_SIZE>& cid);
  zx_status_t MmcSetRelativeAddr(uint16_t rca);
  zx_status_t MmcSendCsd(std::array<uint8_t, SDMMC_CSD_SIZE>& csd);
  zx_status_t MmcSendExtCsd(std::array<uint8_t, MMC_EXT_CSD_SIZE>& ext_csd);
  zx_status_t MmcSelectCard();
  zx_status_t MmcSwitch(uint8_t index, uint8_t value);

 private:
  static constexpr uint32_t kRetryAttempts = 10;

  // Retry each request retries_ times (with wait_time delay in between) by default. Requests are
  // always tried at least once.
  zx_status_t Request(sdmmc_req_t* req, uint32_t retries = 0, zx::duration wait_time = {}) const;
  zx_status_t SdSendAppCmd();

  inline uint32_t RcaArg() const { return rca_ << 16; }

  const ddk::SdmmcProtocolClient host_;
  sdmmc_host_info_t host_info_;
  sdmmc_voltage_t signal_voltage_ = SDMMC_VOLTAGE_V330;
  uint16_t rca_ = 0;  // APP_CMD requires the initial RCA to be zero.
  uint32_t retries_ = 0;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_DEVICE_H_
