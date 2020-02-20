// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/backlight/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void usage(char* argv[]) {
  printf("Usage: %s [--read|--off|<brightness-val>]\n", argv[0]);
  printf("options:\n    <brightness-val>: 0.0-1.0\n");
}

namespace FidlBacklight = llcpp::fuchsia::hardware::backlight;

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    usage(argv);
    return -1;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("Failed to create channel: %d\n", status);
    return -1;
  }

  status = fdio_service_connect("/dev/class/backlight/000", remote.release());
  if (status != ZX_OK) {
    printf("Failed to open backlight: %d\n", status);
    return -1;
  }

  FidlBacklight::Device::SyncClient client(std::move(local));

  if (strcmp(argv[1], "--read") == 0) {
    auto response = client.GetStateNormalized();
    zx_status_t status = response.ok()
                             ? (response->result.is_err() ? response->result.err() : ZX_OK)
                             : response.status();
    if (status != ZX_OK) {
      printf("Get backlight state failed with %d\n", status);
      return -1;
    }
    auto state = response->result.response().state;
    printf("Backlight:%s Brightness:%f\n", state.backlight_on ? "on" : "off", state.brightness);
    return 0;
  }

  bool on;
  double brightness;
  if (strcmp(argv[1], "--off") == 0) {
    on = false;
    brightness = 0.0;
  } else {
    char* endptr;
    brightness = strtod(argv[1], &endptr);
    if (endptr == argv[1] || *endptr != '\0') {
      usage(argv);
      return -1;
    }
    if (brightness < 0.0 || brightness > 1.0) {
      printf("Invalid brightness %f\n", brightness);
      return -1;
    }
    on = true;
  }

  FidlBacklight::State state = {.backlight_on = on, .brightness = brightness};

  auto response = client.SetStateNormalized(state);
  status = response.ok() ? (response->result.is_err() ? response->result.err() : ZX_OK)
                         : response.status();
  if (status != ZX_OK) {
    printf("Set brightness failed with %d\n", status);
    return -1;
  }
  return 0;
}
