// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Attempts to unbind (remove) a device from the system.

unbind device

WARNING: In general this is not a safe operation and removing a device may
result in system instability or even a completely unusable system.
)""";

struct Config {
  const char* path;
};

bool GetOptions(int argc, char** argv, Config* config) {
  while (true) {
    struct option options[] = {
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "h", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'h':
        return false;
    }
  }
  if (argc == optind + 1) {
    config->path = argv[optind];
    return true;
  }
  return false;
}

bool ValidateOptions(const Config& config) {
  if (!config.path) {
    printf("Device path needed\n");
    printf("%s\n", kUsageMessage);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config config = {};
  if (!GetOptions(argc, argv, &config)) {
    printf("%s\n", kUsageMessage);
    return -1;
  }

  if (!ValidateOptions(config)) {
    return -1;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("Could not create channel\n");
    return -1;
  }
  status = fdio_service_connect(config.path, remote.release());
  if (status != ZX_OK) {
    printf("Unable to open device\n");
    return -1;
  }

  auto resp = fuchsia_device::Controller::Call::ScheduleUnbind(zx::unowned_channel(local.get()));
  status = resp.status();

  if (status == ZX_OK) {
    if (resp->result.is_err()) {
      status = resp->result.err();
    }
  }
  if (status != ZX_OK) {
    printf("Failed to unbind device\n");
    return -1;
  }

  printf("Command sent. The device may be gone now\n");
  return 0;
}
