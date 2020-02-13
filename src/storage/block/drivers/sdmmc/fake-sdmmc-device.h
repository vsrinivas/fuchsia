// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_
#define SRC_STORAGE_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/driver/binding.h>

#include <array>
#include <map>
#include <vector>

#include <ddktl/protocol/sdio.h>
#include <ddktl/protocol/sdmmc.h>
#include <fbl/span.h>

namespace sdmmc {

class Bind : public fake_ddk::Bind {
 public:
  int total_children() const { return total_children_; }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override;
  zx_status_t DeviceRemove(zx_device_t* device) override;
  void DeviceAsyncRemove(zx_device_t* device) override;
  void Ok();

  zx_status_t GetChildProtocol(size_t index, uint32_t proto_id, void* proto) {
    if (index >= children_get_proto_.size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    return children_get_proto_[index].op(children_get_proto_[index].ctx, proto_id, proto);
  }

  fbl::Span<const zx_device_prop_t> GetChildProps(size_t index) {
    if (index >= children_props_.size()) {
      return fbl::Span<zx_device_prop_t>();
    }
    return fbl::Span(children_props_[index].data(), children_props_[index].size());
  }

 private:
  struct GetProtocolOp {
    void* ctx;
    zx_status_t (*op)(void* ctx, uint32_t proto_id, void* protocol);
  };

  zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0x1234);
  zx_device_t* kUnknownDevice = reinterpret_cast<zx_device_t*>(0x5678);

  int total_children_ = 0;
  int children_ = 0;

  bool bad_parent_ = false;
  bool bad_device_ = false;
  bool add_called_ = false;
  bool remove_called_ = false;

  void* unbind_ctx_ = nullptr;
  void (*unbind_op_)(void* ctx) = nullptr;

  std::vector<GetProtocolOp> children_get_proto_;
  std::vector<std::vector<zx_device_prop_t>> children_props_;
};

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

  FakeSdmmcDevice() : proto_{.ops = &sdmmc_protocol_ops_, .ctx = this}, host_info_({}) {}

  ddk::SdmmcProtocolClient GetClient() const { return ddk::SdmmcProtocolClient(&proto_); }

  void set_host_info(const sdmmc_host_info_t& host_info) { host_info_ = host_info; }

  const std::map<Command, uint32_t>& command_counts() const { return command_counts_; }

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

  std::vector<uint8_t> Read(size_t address, size_t size, uint8_t func = 0);
  void Write(size_t address, fbl::Span<const uint8_t> data, uint8_t func = 0);
  template <typename T>
  void Write(size_t address, const T& data, uint8_t func = 0) {
    Write(address, fbl::Span<const uint8_t>(data.data(), data.size() * sizeof(data[0])), func);
  }

  void TriggerInBandInterrupt();

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
  const sdmmc_protocol_t proto_;
  sdmmc_host_info_t host_info_;
  std::array<std::map<size_t, std::unique_ptr<uint8_t[]>>, SDIO_MAX_FUNCS> sectors_;
  std::map<Command, uint32_t> command_counts_;
  std::map<Command, CommandCallback> command_callbacks_;
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
};

}  // namespace sdmmc

#endif  // SRC_STORAGE_BLOCK_DRIVERS_SDMMC_FAKE_SDMMC_DEVICE_H_
