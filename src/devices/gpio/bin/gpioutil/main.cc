// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include <fbl/unique_fd.h>

#include "gpioutil.h"

static void usage() {
  printf("Usage: gpioutil <command> <device|name> [<args>]\n\n");
  printf("Read from, write to, and configure the GPIO pin of <device|name>.\n\n");
  printf("Commands:\n");
  printf("  list | l          List the known GPIOs. Both pin and name are printed.\n");
  printf("  name | n          Read the pin and name info of <device|name>.\n");
  printf("  read | r          Read the current value of <device>. Possible return values:\n");
  printf("                    0 (LOW), 1 (HIGH)\n");
  printf("  write | w         Write to <device>. Accepted <args>:\n");
  printf("                    0 (LOW), 1 (HIGH)\n");
  printf("  in | i            Configure <device> as IN. <args> should be the resistor pull.\n");
  printf("                    Accepted <args>: 0 (GPIO_PULL_DOWN), 1 (GPIO_PULL_UP),\n");
  printf("                    2 (GPIO_NO_PULL)\n");
  printf("  out | o           Configure <device> as OUT. <args> should be the initial\n");
  printf("                    value. Accepted <args>: 0 (LOW), 1 (HIGH)\n");
  printf("  drive | d         Set the drive strength of <device>. <args> should be the\n");
  printf("                    drive strength value in microamps.\n");
  printf("  help | h          Print this help text.\n\n");
  printf("Examples:\n");
  printf("  List GPIO pins.\n");
  printf("  $ gpioutil list\n");
  printf(
      "  > [gpio-0] GPIO_HW_ID_3\n"
      "    [gpio-1] GPIO_SOC_TH_BOOT_MODE_L\n"
      "    ...\n\n");
  printf("  Get name from a GPIO pin.\n");
  printf("  $ gpioutil name /dev/sys/platform/05:05:1/gpio/gpio-0\n");
  printf("  > GPIO Name: [gpio-0] GPIO_HW_ID_3\n\n");
  printf("  Read from a GPIO pin.\n");
  printf("  $ gpioutil read /dev/sys/platform/05:05:1/gpio/gpio-0\n");
  printf("  > GPIO Value: 1\n\n");
  printf("  Write a LOW value to a GPIO pin.\n");
  printf("  $ gpioutil write /dev/sys/platform/05:05:1/gpio/gpio-0 0\n\n");
  printf("  Configure a GPIO pin as IN with a pull-down resistor.\n");
  printf("  $ gpioutil in /dev/sys/platform/05:05:1/gpio/gpio-0 0\n\n");
  printf("  Configure a GPIO pin as OUT with an initial value of HIGH.\n");
  printf("  $ gpioutil out /dev/sys/platform/05:05:1/gpio/gpio-0 1\n\n");
  printf("  Get the current drive strength in microamps of a GPIO pin.\n");
  printf("  $ gpioutil drive /dev/sys/platform/05:05:1/gpio/gpio-0\n");
  printf("  Drive Strength: 500 ua\n\n");
  printf("  Set the drive strength of a GPIO pin to 500 microamps.\n");
  printf("  $ gpioutil drive /dev/sys/platform/05:05:1/gpio/gpio-0 500\n");
  printf("  > Set drive strength to 500\n\n");
}

int main(int argc, char** argv) {
  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  if (ParseArgs(argc, argv, &func, &write_value, &in_flag, &out_value, &ds_ua)) {
    fprintf(stderr, "Unable to parse arguments!\n\n");
    usage();
    return -1;
  }

  // Handle functions without any parameter.
  if (func == List) {
    return ListGpios();
  }

  int ret = 0;
  if (access(argv[2], F_OK) == 0) {
    // Access by device path
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      fprintf(stderr, "Unable to create channel!\n\n");
      usage();
      return -1;
    }

    status = fdio_service_connect(argv[2], remote.release());
    if (status != ZX_OK) {
      fprintf(stderr, "Unable to connect to device!\n\n");
      usage();
      return -1;
    }

    ret = ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>(std::move(local)), func,
                     write_value, in_flag, out_value, ds_ua);
  } else {
    // Access by GPIO name
    auto client = FindGpioClientByName(argv[2]);
    if (!client) {
      fprintf(stderr, "Unable to connect GPIO by name %s\n\n", argv[2]);
      usage();
      return -1;
    }

    ret = ClientCall(std::move(*client), func, write_value, in_flag, out_value, ds_ua);
  }

  if (ret == -1) {
    fprintf(stderr, "Client call failed!\n\n");
    usage();
  }
  return ret;
}
