// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
#define SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_

#include <fuchsia/hardware/gpio/llcpp/fidl.h>
#include <lib/zx/status.h>
#include <stdio.h>

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

enum GpioFunc { Read, Write, ConfigIn, ConfigOut, SetDriveStrength, Invalid };

template <typename T, typename ReturnType>
zx::status<ReturnType> GetStatus(const T& result);

template <typename T>
zx::status<> GetStatus(const T& result);

// Parse the command line arguments in |argv|
int ParseArgs(int argc, char** argv, GpioFunc* func, uint8_t* write_value,
              ::llcpp::fuchsia::hardware::gpio::GpioFlags* in_flag, uint8_t* out_value,
              uint64_t* ds_ua);

int ClientCall(::llcpp::fuchsia::hardware::gpio::Gpio::SyncClient client, GpioFunc func,
               uint8_t write_value, ::llcpp::fuchsia::hardware::gpio::GpioFlags in_flag,
               uint8_t out_value, uint64_t ds_ua);

#endif  // SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
