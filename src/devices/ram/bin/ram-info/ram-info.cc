// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-info.h"

#include <lib/fdio/fdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <soc/aml-common/aml-ram.h>

namespace ram_metrics = fuchsia_hardware_ram_metrics;

namespace {

// TODO(fxbug.dev/48254): Get default channel information through the FIDL API.

constexpr ram_info::RamDeviceInfo kDevices[] = {
    {
        // Astro
        .devfs_path = "/dev/sys/platform/05:03:24/ram",
        .default_cycles_to_measure = 456000000 / 20,  // 456 Mhz, 50 ms.
        .default_channels =
            {
                [0] = {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
                [1] = {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
                [2] = {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
                [3] = {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
            },
    },
    {
        // Sherlock
        .devfs_path = "/dev/sys/platform/05:04:24/ram",
        .default_cycles_to_measure = 792000000 / 20,  // 792 Mhz, 50 ms.
        .default_channels =
            {
                [0] = {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
                [1] = {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
                [2] = {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
                [3] = {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
            },
    },
    {
        // Nelson
        .devfs_path = "/dev/sys/platform/05:05:24/ram",
        .default_cycles_to_measure = 456000000 / 20,  // 456 Mhz, 50 ms.
        .default_channels =
            {
                [0] = {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
                [1] = {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
                [2] = {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
                [3] = {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
            },
    },
};

double CounterToBandwidthMBs(uint64_t cycles, uint64_t frequency, uint64_t cycles_measured,
                             uint64_t bytes_per_cycle) {
  double bandwidth_rw = static_cast<double>(cycles * frequency * bytes_per_cycle);
  bandwidth_rw /= static_cast<double>(cycles_measured);
  bandwidth_rw /= 1024.0 * 1024.0;
  return bandwidth_rw;
}

}  // namespace

namespace ram_info {

void DefaultPrinter::Print(const ram_metrics::wire::BandwidthInfo& info) const {
  fprintf(file_, "channel \t\t usage (MB/s)  time: %lu ms\n", info.timestamp / ZX_MSEC(1));
  size_t ix = 0;
  double total_bandwidth_rw = 0;
  for (const auto& row : rows_) {
    if (row.empty()) {
      continue;
    }
    // We discard read-only and write-only counters as they are not supported
    // by current hardware.
    double bandwidth_rw = CounterToBandwidthMBs(info.channels[ix].readwrite_cycles, info.frequency,
                                                cycles_to_measure_, info.bytes_per_cycle);
    total_bandwidth_rw += bandwidth_rw;
    fprintf(file_, "%s (rw) \t\t %g\n", row.c_str(), bandwidth_rw);
    ++ix;
  }
  // Use total read-write cycles if supported.
  if (info.total.readwrite_cycles) {
    total_bandwidth_rw = CounterToBandwidthMBs(info.total.readwrite_cycles, info.frequency,
                                               cycles_to_measure_, info.bytes_per_cycle);
  }
  fprintf(file_, "total (rw) \t\t %g\n", total_bandwidth_rw);
}

void CsvPrinter::Print(const ram_metrics::wire::BandwidthInfo& info) const {
  size_t row_count = 0;
  for (const auto& row : rows_) {
    if (!row.empty()) {
      row_count++;
    }
  }

  fprintf(file_, "time,");

  size_t ix = 0;
  for (const auto& row : rows_) {
    if (row.empty()) {
      continue;
    }

    fprintf(file_, "\"%s\"%s", row.c_str(), (ix < row_count - 1) ? "," : "");
    ix++;
  }

  fprintf(file_, "\n%lu,", info.timestamp / ZX_MSEC(1));

  ix = 0;
  for (const auto& row : rows_) {
    if (row.empty()) {
      continue;
    }

    double bandwidth_rw = CounterToBandwidthMBs(info.channels[ix].readwrite_cycles, info.frequency,
                                                cycles_to_measure_, info.bytes_per_cycle);
    fprintf(file_, "%g%s", bandwidth_rw, (ix < row_count - 1) ? "," : "\n");
    ix++;
  }
}

zx::status<std::array<uint64_t, ram_metrics::wire::MAX_COUNT_CHANNELS>> ParseChannelString(
    std::string_view str) {
  if (str[0] == '\0') {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  std::array<uint64_t, ram_metrics::wire::MAX_COUNT_CHANNELS> channels = {};
  std::string_view next_channel = str;

  for (uint64_t& channel : channels) {
    errno = 0;
    char* endptr;
    channel = strtoul(next_channel.data(), &endptr, 0);
    if (endptr > &(*next_channel.cend())) {
      return zx::error_status(ZX_ERR_BAD_STATE);
    }

    next_channel = endptr;

    if (channel == ULONG_MAX && errno == ERANGE) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    if (next_channel[0] == '\0') {
      break;
    }

    // Only a comma separator is allowed.
    if (next_channel[0] != ',') {
      return zx::error_status(ZX_ERR_INVALID_ARGS);
    }

    next_channel = next_channel.data() + 1;
  }

  // Make sure there are no trailing characters.
  if (next_channel[0] != '\0') {
    return zx::error_status(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(channels);
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
                            const ram_metrics::wire::BandwidthMeasurementConfig& config) {
  fidl::WireSyncClient<ram_metrics::Device> client{std::move(channel)};
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

zx_status_t GetDdrWindowingResults(zx::channel channel) {
  fidl::WireSyncClient<ram_metrics::Device> client{std::move(channel)};
  auto info = client.GetDdrWindowingResults();
  if (!info.ok()) {
    return info.status();
  }
  if (info->result.is_err()) {
    return info->result.err();
  }

  printf("register value: 0x%x\n", info->result.response().value);
  return ZX_OK;
}

}  // namespace ram_info
