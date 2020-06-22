// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_CPU_STRESS_H_
#define GARNET_BIN_HWSTRESS_CPU_STRESS_H_

#include <lib/zx/time.h>

#include "args.h"
#include "status.h"
#include "temperature_sensor.h"

namespace hwstress {

// Start a stress test.
//
// Return true on success.
bool StressCpu(StatusLine* status, const CommandLineArgs& args, zx::duration duration_seconds,
               TemperatureSensor* sensor = GetNullTemperatureSensor());

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_CPU_STRESS_H_
