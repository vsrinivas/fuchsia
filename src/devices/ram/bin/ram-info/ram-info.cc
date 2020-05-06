// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-info.h"

#include <lib/fdio/fdio.h>

#include <fbl/unique_fd.h>
#include <soc/aml-common/aml-ram.h>

namespace ram_metrics = ::llcpp::fuchsia::hardware::ram::metrics;

namespace {

// TODO(48254): Get default channel information through the FIDL API.

constexpr ram_info::RamDeviceInfo kDevices[] = {
    {
        // Astro
        .devfs_path = "/dev/sys/platform/05:03:24/ram",
        .default_cycles_to_measure = aml_ram::kMemCycleCount,
        .default_channels =
            {
                [0] = {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
                [1] = {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
                [2] = {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
                [3] = {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
            },
        .counter_to_bandwidth_mbs = aml_ram::CounterToBandwidth,
    },
    {
        // Sherlock
        .devfs_path = "/dev/sys/platform/05:04:24/ram",
        .default_cycles_to_measure = aml_ram::kMemCycleCount,
        .default_channels =
            {
                [0] = {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
                [1] = {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
                [2] = {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
                [3] = {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
            },
        .counter_to_bandwidth_mbs = aml_ram::CounterToBandwidth,
    },
};

}  // namespace

namespace ram_info {

void DefaultPrinter::Print(const ram_metrics::BandwidthInfo& info) const {
  fprintf(file_, "channel \t\t usage (MB/s)  time: %lu ms\n", info.timestamp / ZX_MSEC(1));
  size_t ix = 0;
  double total_bandwith_rw = 0;
  for (const auto& row : rows_) {
    if (row.empty()) {
      continue;
    }
    // We discard read-only and write-only counters as they are not supported
    // by current hardware.
    double bandwith_rw = device_info_.counter_to_bandwidth_mbs(info.channels[ix].readwrite_cycles);
    total_bandwith_rw += bandwith_rw;
    fprintf(file_, "%s (rw) \t\t %g\n", row.c_str(), bandwith_rw);
    ++ix;
  }
  fprintf(file_, "total (rw) \t\t %g\n", total_bandwith_rw);
}

std::tuple<zx::channel, ram_info::RamDeviceInfo> ConnectToRamDevice() {
  for (const auto& info : kDevices) {
    fbl::unique_fd fd(open(info.devfs_path, O_RDWR));
    if (fd.get() <= -1) {
      continue;
    }

    zx::channel handle;
    zx_status_t status = fdio_get_service_handle(fd.release(), handle.reset_and_get_address());
    if (status == ZX_OK) {
      return {std::move(handle), info};
    }
  }

  return {};
}

zx_status_t MeasureBandwith(const Printer* const printer, zx::channel channel,
                            const ram_metrics::BandwidthMeasurementConfig& config) {
  ram_metrics::Device::SyncClient client{std::move(channel)};
  auto info = client.MeasureBandwidth(config);
  if (!info.ok()) {
    return info.status();
  }
  if (info->result.is_err()) {
    return info->result.err();
  }

  printer->Print(info->result.response().info);
  return ZX_OK;
}

}  // namespace ram_info
