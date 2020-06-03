// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <fbl/algorithm.h>

#include "ram-info.h"

namespace ram_metrics = ::llcpp::fuchsia::hardware::ram::metrics;

static constexpr char kVersionString[] = "1";

static void PrintUsage(const char* cmd) {
  fprintf(stderr, "\nQuery RAM bandwith\n");
  fprintf(stderr, "\t%s             Print default domain values\n", cmd);
  fprintf(stderr, "\t%s --help      Print this message and quit.\n", cmd);
  fprintf(stderr, "\t%s --version   Print version and quit.\n", cmd);
  fprintf(stderr, "\t%s --csv       Print RAM bandwidth in CSV format.\n", cmd);
  fprintf(stderr, "\t%s --channels|-c <channel0[,channel1,...]>\n", cmd);
  fprintf(stderr, "\t\t Use the specified port masks instead of the device defaults.\n");
  fprintf(stderr, "\t\t For example: %s --channels 0x17,0xc,16.\n", cmd);
}

int main(int argc, char* argv[]) {
  bool use_csv = false;
  std::optional<std::array<uint64_t, ram_metrics::MAX_COUNT_CHANNELS>> channels = {};

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      PrintUsage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--version") == 0) {
      printf("%s\n", kVersionString);
      return 0;
    }

    if (strcmp(argv[i], "--csv") == 0) {
      use_csv = true;
    } else if (strcmp(argv[i], "--channels") == 0 || strcmp(argv[i], "-c") == 0) {
      if (i == argc - 1) {
        PrintUsage(argv[0]);
        return 1;
      }

      auto result = ram_info::ParseChannelString(argv[++i]);
      if (result.is_error()) {
        PrintUsage(argv[0]);
        return 1;
      }

      channels.emplace(result.value());
    } else {
      PrintUsage(argv[0]);
      return 1;
    }
  }

  auto [channel, device_info] = ram_info::ConnectToRamDevice();
  if (!channel.is_valid()) {
    fprintf(stderr, "unable to connect to ram device, the target might not be supported\n");
    return 1;
  }

  ram_info::DefaultPrinter default_printer(stdout, device_info);
  ram_info::CsvPrinter csv_printer(stdout, device_info);

  ram_info::Printer* printer = &default_printer;
  if (use_csv) {
    printer = &csv_printer;
  }

  ::llcpp::fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = device_info.default_cycles_to_measure;

  if (channels) {
    for (size_t i = 0; i < channels->size(); i++) {
      printer->AddChannelName(i, "channel " + std::to_string(i));
      config.channels[i] = channels->at(i);
    }
  } else {
    for (size_t i = 0; i < std::size(device_info.default_channels); i++) {
      if (device_info.default_channels[i].name == nullptr) {
        break;
      }

      printer->AddChannelName(i, device_info.default_channels[i].name);
      config.channels[i] = device_info.default_channels[i].mask;
    }
  }

  zx_status_t status = MeasureBandwith(printer, std::move(channel), config);
  if (status != ZX_OK) {
    fprintf(stderr, "failed to measure bandwidth: %d\n", status);
    return -1;
  }

  return 0;
}
