// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
#define SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_

#include <fidl/fuchsia.hardware.gpio/cpp/wire.h>
#include <lib/zx/status.h>
#include <stdio.h>

enum GpioFunc {
  Read,
  Write,
  ConfigIn,
  ConfigOut,
  SetDriveStrength,
  GetDriveStrength,
  GetName,
  List,
  Invalid
};

template <typename T, typename ReturnType>
zx::result<ReturnType> GetStatus(const T& result);

template <typename T>
zx::result<> GetStatus(const T& result);

// Parse the command line arguments in |argv|
int ParseArgs(int argc, char** argv, GpioFunc* func, uint8_t* write_value,
              fuchsia_hardware_gpio::wire::GpioFlags* in_flag, uint8_t* out_value, uint64_t* ds_ua);

int ListGpios(void);

std::optional<fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio>> FindGpioClientByName(
    std::string_view name);

int ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio> client, GpioFunc func,
               uint8_t write_value, fuchsia_hardware_gpio::wire::GpioFlags in_flag,
               uint8_t out_value, uint64_t ds_ua);

#endif  // SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
