// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_
#define SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_

#include <fuchsia/hardware/ram/metrics/llcpp/fidl.h>
#include <lib/zx/status.h>
#include <stdio.h>

#include <array>
#include <string>
#include <string_view>
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
};

class Printer {
 public:
  Printer(FILE* file, uint64_t cycles_to_measure)
      : file_(file),
        rows_(::llcpp::fuchsia::hardware::ram::metrics::MAX_COUNT_CHANNELS),
        cycles_to_measure_(cycles_to_measure) {}
  virtual ~Printer() = default;

  void AddChannelName(size_t channel_index, const std::string& name) {
    rows_[channel_index] = name;
  }

  virtual void Print(const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthInfo& bpi) const = 0;

 protected:
  FILE* const file_;
  std::vector<std::string> rows_;
  const uint64_t cycles_to_measure_;
};

class DefaultPrinter : public Printer {
 public:
  DefaultPrinter(FILE* file, uint64_t cycles_to_measure) : Printer(file, cycles_to_measure) {}
  void Print(const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthInfo& info) const override;
};

class CsvPrinter : public Printer {
 public:
  CsvPrinter(FILE* file, uint64_t cycles_to_measure) : Printer(file, cycles_to_measure) {}
  void Print(const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthInfo& info) const override;
};

zx::status<std::array<uint64_t, ::llcpp::fuchsia::hardware::ram::metrics::MAX_COUNT_CHANNELS>>
ParseChannelString(std::string_view str);

std::tuple<zx::channel, ram_info::RamDeviceInfo> ConnectToRamDevice();

zx_status_t MeasureBandwith(
    const Printer* printer, zx::channel channel,
    const ::llcpp::fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig& config);

zx_status_t GetDdrWindowingResults(zx::channel channel);

}  // namespace ram_info

#endif  // SRC_DEVICES_RAM_BIN_RAM_INFO_RAM_INFO_H_
