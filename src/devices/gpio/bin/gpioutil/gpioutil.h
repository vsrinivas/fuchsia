// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
#define SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_

#include <fuchsia/hardware/gpio/llcpp/fidl.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <zircon/status.h>

enum GpioFunc { Read = 0, Write = 1, ConfigIn = 2, ConfigOut = 3, Invalid = 4 };

template <typename T, typename ReturnType>
zx::status<ReturnType> GetStatus(const T& result);

template <typename T>
zx::status<> GetStatus(const T& result);

// Parse the command line arguments in |argv|
int ParseArgs(int argc, char** argv, GpioFunc* func, uint8_t* write_value,
              ::llcpp::fuchsia::hardware::gpio::GpioFlags* in_flag, uint8_t* out_value);

int ClientCall(::llcpp::fuchsia::hardware::gpio::Gpio::SyncClient client, GpioFunc func,
               uint8_t write_value, ::llcpp::fuchsia::hardware::gpio::GpioFlags in_flag,
               uint8_t out_value);

#endif  // SRC_DEVICES_GPIO_BIN_GPIOUTIL_GPIOUTIL_H_
