// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pwm/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>

#include <string>

#include <fbl/unique_fd.h>

#include "pwmctl.h"

void usage(int argc, char const* argv[]) {
  fprintf(stderr, "Usage: %s <device> <command> [args]\n", argv[0]);
  fprintf(stderr, "enable                    Enables the PWM\n");
  fprintf(stderr, "disable                   Disables the PWM\n");
  fprintf(stderr, "config <pol> <per> <d>    Sets the polarity (pol), and\n");
  fprintf(stderr, "                          period (per) and duty cycle (d) of the PWM.\n");
  fprintf(stderr, "                          Polarity must be 0 or 1\n");
  fprintf(stderr, "                          Period must be a positive integer in nanoseconds\n");
  fprintf(stderr, "                          Duty cycle must be a float [0.0, 100.0]\n");
}

int main(int argc, char const* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Expected at least 3 arguments\n");
    usage(argc, argv);
    return -1;
  }

  const std::string devpath(argv[1]);
  fbl::unique_fd device(open(devpath.c_str(), O_RDWR));
  if (!device.is_valid()) {
    fprintf(stderr, "Failed to open %s: %s\n", devpath.c_str(), strerror(errno));
    return -1;
  }

  fidl::ClientEnd<fuchsia_hardware_pwm::Pwm> pwm_client;
  zx_status_t status =
      fdio_get_service_handle(device.release(), pwm_client.channel().reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(status));
    return -1;
  }

  zx_status_t st = pwmctl::run(argc, argv, std::move(pwm_client));

  return st == ZX_OK ? 0 : -1;
}
