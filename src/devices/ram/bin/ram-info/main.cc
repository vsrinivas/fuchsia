// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "ram-info.h"

// TODO(48254): Add an option to print in CSV format.
// TODO(48254): Add an option to specify channel masks.

static constexpr char kVersionString[] = "1";

static void PrintUsage(const char* cmd) {
  fprintf(stderr, "\nQuery RAM bandwith\n");
  fprintf(stderr, "\t%s             Print default domain values\n", cmd);
  fprintf(stderr, "\t%s --help      Print this message and quit.\n", cmd);
  fprintf(stderr, "\t%s --version   Print version and quit.\n", cmd);
}

int main(int argc, char* argv[]) {
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    printf("%s\n", kVersionString);
    return 0;
  }

  if (argc != 1) {
    PrintUsage(argv[0]);
    return 0;
  }

  auto [channel, device_info] = ram_info::ConnectToRamDevice();
  if (!channel.is_valid()) {
    fprintf(stderr, "unable to connect to ram device, the target might not be supported\n");
    return 1;
  }

  ram_info::DefaultPrinter printer(stdout, device_info);

  ::llcpp::fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = device_info.default_cycles_to_measure;

  for (size_t i = 0; i < fbl::count_of(device_info.default_channels); i++) {
    if (device_info.default_channels[i].name == nullptr) {
      break;
    }

    printer.AddChannelName(i, device_info.default_channels[i].name);
    config.channels[i] = device_info.default_channels[i].mask;
  }

  zx_status_t status = MeasureBandwith(&printer, std::move(channel), config);
  if (status != ZX_OK) {
    fprintf(stderr, "failed to measure bandwidth: %d\n", status);
    return -1;
  }

  return 0;
}
