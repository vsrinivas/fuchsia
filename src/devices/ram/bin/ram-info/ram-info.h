// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_
#define SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_

#include <fuchsia/hardware/ram/metrics/llcpp/fidl.h>
#include <stdio.h>

#include <string>
#include <tuple>
#include <vector>

namespace ram_info {

struct RamDeviceInfo {
  const char* devfs_path;
  uint64_t default_cycles_to_measure;
  struct {
    const char* name;
    uint64_t mask;
  } default_channels[::llcpp::fuchsia::hardware::ram::metrics::MAX_COUNT_CHANNELS];
  // Counter value to bandwidth in MB/s.
  double (*counter_to_bandwidth_mbs)(uint64_t counter);
};

class Printer {
 public:
  Printer(FILE* file, const RamDeviceInfo& device_info)
      : file_(file),
        rows_(::llcpp::fuchsia::hardware::ram::metrics::MAX_COUNT_CHANNELS),
        device_info_(device_info) {}
  virtual ~Printer() = default;

  void AddChannelName(size_t channel_index, const std::string& name) {
    rows_[channel_index] = name;
  }

  virtual void Print(const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthInfo& bpi) const = 0;

 protected:
  FILE* const file_;
  std::vector<std::string> rows_;
  const RamDeviceInfo device_info_;
};

class DefaultPrinter : public Printer {
 public:
  DefaultPrinter(FILE* file, const RamDeviceInfo& device_info) : Printer(file, device_info) {}
  void Print(const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthInfo& info) const override;
};

std::tuple<zx::channel, ram_info::RamDeviceInfo> ConnectToRamDevice();

zx_status_t MeasureBandwith(
    const Printer* printer, zx::channel channel,
    const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig& config);

}  // namespace ram_info

#endif  // SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_
