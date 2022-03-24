// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/subdriver.h"

#include <lib/ddk/debug.h>

#include "chromiumos-platform-ec/ec_commands.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"

namespace chromiumos_ec_core {
namespace {

// Supported drivers and the features that they rely on.
// For unit testing, this file provides a weak implementation of all of the bind() functions.
constexpr struct FeatureDriver {
  void (*bind)(ChromiumosEcCore*);
  size_t feature;
} kFeatureDrivers[] = {
    {
        .bind = motion::RegisterMotionDriver,
        .feature = EC_FEATURE_MOTION_SENSE,
    },
    {
        .bind = usb_pd::RegisterUsbPdDriver,
        .feature = EC_FEATURE_USB_PD,
    },
};

// Drivers that rely on a specific board.
// For unit testing, this file provides a weak implementation of all of the bind() functions.
constexpr struct BoardDriver {
  void (*bind)(ChromiumosEcCore*);
  const char* board;
} kBoardDrivers[] = {
    {
        .bind = power_sensor::RegisterPowerSensorDriver,
        .board = kAtlasBoardName,
    },
};

}  // namespace

void BindSubdrivers(ChromiumosEcCore* ec) {
  for (auto& driver : kFeatureDrivers) {
    if (ec->HasFeature(driver.feature)) {
      driver.bind(ec);
    }
  }

  for (auto& driver : kBoardDrivers) {
    if (ec->IsBoard(driver.board)) {
      driver.bind(ec);
    }
  }
}

#define WEAK_REGISTER_SYMBOL(ns, type)                                      \
  namespace ns {                                                            \
  void __attribute__((weak)) Register##type##Driver(ChromiumosEcCore* ec) { \
    zxlogf(INFO, #type " driver not supported");                            \
  }                                                                         \
  }  // namespace ns

WEAK_REGISTER_SYMBOL(motion, Motion)
WEAK_REGISTER_SYMBOL(usb_pd, UsbPd)
WEAK_REGISTER_SYMBOL(power_sensor, PowerSensor)

}  // namespace chromiumos_ec_core
