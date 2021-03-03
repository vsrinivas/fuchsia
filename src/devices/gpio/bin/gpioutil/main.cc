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
  printf("usage:\n");
  printf("    gpioutil h                          [Prints help message (this)]\n");
  printf("    gpioutil r DEVICE                   [Read from GPIO]\n");
  printf("    gpioutil w DEVICE value             [Write to GPIO <value>]\n");
  printf("    gpioutil i DEVICE flags             [Config GPIO as IN with <flags>]\n");
  printf("        available flags: 0 - GPIO_PULL_DOWN\n");
  printf("                         1 - GPIO_PULL_UP\n");
  printf("                         2 - GPIO_NO_PULL\n");
  printf("    gpioutil o DEVICE initial_value     [Config GPIO as OUT with <initial_value>]\n\n");
  printf(
      "    gpioutil d DEVICE uA                [Set GPIO Drive Strength to <uA> (microAmperes)]\n");
  printf(
      "     * DEVICE is path to device. Sample: "
      "/dev/sys/platform/05:04:1/aml-axg-gpio/gpio-<pin>,\n");
  printf("       where <pin> corresponds to the pin number calculated for it. For example, see\n");
  printf("       calculation in lib/amlogic/include/soc/aml-t931/t931-gpio.h\n");
}

int main(int argc, char** argv) {
  GpioFunc func;
  uint8_t write_value, out_value;
  uint64_t ds_ua;
  ::fuchsia_hardware_gpio::wire::GpioFlags in_flag;
  if (ParseArgs(argc, argv, &func, &write_value, &in_flag, &out_value, &ds_ua)) {
    printf("Unable to parse arguments!\n\n");
    usage();
    return -1;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("Unable to create channel!\n\n");
    usage();
    return -1;
  }
  status = fdio_service_connect(argv[2], remote.release());
  if (status != ZX_OK) {
    printf("Unable to connect to device!\n\n");
    usage();
    return -1;
  }

  int ret = ClientCall(::fuchsia_hardware_gpio::Gpio::SyncClient(std::move(local)), func,
                       write_value, in_flag, out_value, ds_ua);
  if (ret == -1) {
    printf("Client call failed!\n\n");
    usage();
  }
  return ret;
}
