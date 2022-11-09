// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/component/cpp/incoming/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "gpioutil.h"

namespace {

constexpr size_t kArgDevice = 2;

}  // namespace

// LINT.IfChange
static void usage() {
  printf(
      "Usage: gpioutil <command> [<name>] [<value>]\n\n"
      "List, read from, write to, and configure GPIOs.\n\n"
      "Commands:\n"
      "  list | l          List the known GPIOs. Each GPIO is represented by 2 values.\n"
      "                    Example: `[gpio-0] GPIO_HW_ID_3`. The value inside the\n"
      "                    brackets (`gpio-0`) can be ignored. The value after the brackets\n"
      "                    (`GPIO_HW_ID_3`) is the <name> value to provide to other gpioutil\n"
      "                    commands. GPIO names are defined in the driver source code and\n"
      "                    usually match the datasheet's name for the GPIO. Example:\n"
      "                    "
      "https://cs.opensource.google/fuchsia/fuchsia/+/main:src/devices/board/drivers/vim3/"
      "vim3-gpio.cc;l=72\n"
      "  read | r          Read the current value of <name>. Possible return values are\n"
      "                    `0` (LOW) or `1` (HIGH).\n"
      "  write | w         Write to <name>. <value> should be `0` (LOW) or `1` (HIGH).\n"
      "  in | i            Configure <name> as IN. <value> is the resistor pull and its value\n"
      "                    should be `0` (GPIO_PULL_DOWN), `1` (GPIO_PULL_UP), or `2` "
      "(GPIO_NO_PULL).\n"
      "  out | o           Configure <name> as OUT. <value> is the initial OUT\n"
      "                    state and its value should be `0` (LOW) or `1` (HIGH).\n"
      "  drive | d         Set the drive strength of <name>. <value> should be the\n"
      "                    drive strength value in microamps.\n"
      "  help | h          Print this help text.\n\n"
      "Examples:\n"
      "  List GPIO pins:\n"
      "  $ gpioutil list\n"
      "  [gpio-0] GPIO_HW_ID_3\n"
      "  [gpio-1] GPIO_SOC_TH_BOOT_MODE_L\n"
      "  ...\n\n"
      "  Read the current value of <name>:\n"
      "  $ gpioutil read GPIO_HW_ID_3\n"
      "  GPIO Value: 1\n\n"
      "  Write a LOW value to a GPIO pin:\n"
      "  $ gpioutil write GPIO_HW_ID_3 0\n\n"
      "  Configure a GPIO pin as IN with a pull-down resistor:\n"
      "  $ gpioutil in GPIO_HW_ID_3 0\n\n"
      "  Configure a GPIO pin as OUT with an initial value of HIGH:\n"
      "  $ gpioutil out GPIO_HW_ID_3 1\n\n"
      "  Get the current drive strength in microamps of a GPIO pin:\n"
      "  $ gpioutil drive GPIO_HW_ID_3\n"
      "  Drive Strength: 500 ua\n\n"
      "  Set the drive strength of a GPIO pin to 500 microamps:\n"
      "  $ gpioutil drive GPIO_HW_ID_3 500\n"
      "  Set drive strength to 500\n\n");
}
// LINT.ThenChange(//docs/reference/tools/hardware/gpioutil.md)

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
  if (access(argv[kArgDevice], F_OK) == 0) {
    // Access by device path
    auto client_end = component::Connect<fuchsia_hardware_gpio::Gpio>(argv[kArgDevice]);

    if (client_end.is_error()) {
      fprintf(stderr, "Failed to get client, st = %d\n", client_end.status_value());
      return -1;
    }
    fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio> client(std::move(*client_end));
    ret = ClientCall(std::move(client), func, write_value, in_flag, out_value, ds_ua);
  } else {
    // Access by GPIO name
    auto client = FindGpioClientByName(argv[kArgDevice]);
    if (client.is_error()) {
      fprintf(stderr, "Unable to connect GPIO by name '%s', st = %d\n\n", argv[kArgDevice],
              client.status_value());
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
