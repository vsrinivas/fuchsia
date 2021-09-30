// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_

#include <fuchsia/hardware/sdio/cpp/banjo.h>
#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <lib/ddk/binding.h>

#include <array>
#include <map>
#include <vector>

#include <ddktl/device.h>
#include <fbl/span.h>

#include "src/lib/vmo_store/vmo_store.h"

namespace sdmmc {

class FakeSdmmcDevice : public ddk::SdmmcProtocol<FakeSdmmcDevice> {
 public:
  using Command = uint32_t;
  using CommandCallback = void (*)(sdmmc_req_t*);

  static constexpr uint32_t kBadRegionStart = 0x0bad00;
  static constexpr uint32_t kBadRegionMask = 0x0fff00;

  // This is the sector size from the eMMC specification. It is valid for cards over 2GB which we
  // assume all of our supported cards will be.
  static constexpr size_t kBlockSize = 512;
  static constexpr size_t kBlockMask = ~static_cast<size_t>(kBlockSize - 1);

  // This is a dummy value, not currently enforced.
  static constexpr size_t kBlockCount = 0x10000;

  FakeSdmmcDevice() : proto_{.ops = &sdmmc_protocol_ops_, .ctx = this}, host_info_({}) {
    for (auto& store : registered_vmos_) {
      store.emplace(vmo_store::Options{});
    }
  }

  ddk::SdmmcProtocolClient GetClient() const { return ddk::SdmmcProtocolClient(&proto_); }

  void set_host_info(const sdmmc_host_info_t& host_info) { host_info_ = host_info; }

  const std::map<Command, uint32_t>& command_counts() const { return command_counts_; }
  std::vector<sdmmc_req_t>& requests() { return requests_; }

  void Reset() {
    for (auto& sector : sectors_) {
      sector.clear();
    }

    command_counts_.clear();
    command_callbacks_.clear();
  }

  zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info);

  zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
    signal_voltage_ = voltage;
    return set_signal_voltage_status_;
  }
  zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
    bus_width_ = bus_width;
    return set_bus_width_status_;
  }
  zx_status_t SdmmcSetBusFreq(uint32_t bus_freq) {
    bus_freq_ = bus_freq;
    return set_bus_freq_status_;
  }
  zx_status_t SdmmcSetTiming(sdmmc_timing_t timing) {
    timing_ = timing;
    return set_timing_status_;
  }
  void SdmmcHwReset() {}
  zx_status_t SdmmcPerformTuning(uint32_t cmd_idx) { return perform_tuning_status_; }

  zx_status_t SdmmcRequest(sdmmc_req_t* req);
  zx_status_t SdmmcRegisterInBandInterrupt(const in_band_interrupt_protocol_t* interrupt_cb);
  zx_status_t SdmmcRegisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo vmo, uint64_t offset,
                               uint64_t size, uint32_t vmo_rights);
  zx_status_t SdmmcUnregisterVmo(uint32_t vmo_id, uint8_t client_id, zx::vmo* out_vmo);
  zx_status_t SdmmcRequestNew(const sdmmc_req_new_t* req, uint32_t out_response[4]);

  std::vector<uint8_t> Read(size_t address, size_t size, uint8_t func = 0);
  void Write(size_t address, fbl::Span<const uint8_t> data, uint8_t func = 0);
  template <typename T>
  void Write(size_t address, const T& data, uint8_t func = 0) {
    Write(address, fbl::Span<const uint8_t>(data.data(), data.size() * sizeof(data[0])), func);
  }
  void Erase(size_t address, size_t size, uint8_t func = 0);

  void TriggerInBandInterrupt() const;

  void set_command_callback(Command command, CommandCallback callback) {
    command_callbacks_[command] = callback;
  }

  void set_set_signal_voltage_status(zx_status_t status) { set_signal_voltage_status_ = status; }
  void set_set_bus_width_status(zx_status_t status) { set_bus_width_status_ = status; }
  void set_set_bus_freq_status(zx_status_t status) { set_bus_freq_status_ = status; }
  void set_set_timing_status(zx_status_t status) { set_timing_status_ = status; }
  void set_perform_tuning_status(zx_status_t status) { perform_tuning_status_ = status; }

  sdmmc_voltage_t signal_voltage() const { return signal_voltage_; }
  sdmmc_bus_width_t bus_width() const { return bus_width_; }
  uint32_t bus_freq() const { return bus_freq_; }
  sdmmc_timing_t timing() const { return timing_; }

 private:
  struct OwnedVmoInfo {
    uint64_t offset;
    uint64_t size;
  };

  using SdmmcVmoStore = vmo_store::VmoStore<vmo_store::HashTableStorage<uint32_t, OwnedVmoInfo>>;

  static zx_status_t CopySdmmcRegions(fbl::Span<const sdmmc_buffer_region_t> regions,
                                      SdmmcVmoStore& vmos, uint8_t* buffer, bool copy_to_regions);

  const sdmmc_protocol_t proto_;
  sdmmc_host_info_t host_info_;
  std::array<std::map<size_t, std::unique_ptr<uint8_t[]>>, SDIO_MAX_FUNCS> sectors_;
  std::map<Command, uint32_t> command_counts_;
  std::map<Command, CommandCallback> command_callbacks_;
  std::vector<sdmmc_req_t> requests_;
  in_band_interrupt_protocol_t interrupt_cb_ = {};
  zx_status_t set_signal_voltage_status_ = ZX_OK;
  zx_status_t set_bus_width_status_ = ZX_OK;
  zx_status_t set_bus_freq_status_ = ZX_OK;
  zx_status_t set_timing_status_ = ZX_OK;
  zx_status_t perform_tuning_status_ = ZX_OK;
  sdmmc_voltage_t signal_voltage_ = SDMMC_VOLTAGE_MAX;
  sdmmc_bus_width_t bus_width_ = SDMMC_BUS_WIDTH_ONE;
  uint32_t bus_freq_ = 0;
  sdmmc_timing_t timing_ = SDMMC_TIMING_MAX;
  std::optional<uint32_t> erase_group_start_;
  std::optional<uint32_t> erase_group_end_;
  std::optional<SdmmcVmoStore> registered_vmos_[SDMMC_MAX_CLIENT_ID + 1];
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_
