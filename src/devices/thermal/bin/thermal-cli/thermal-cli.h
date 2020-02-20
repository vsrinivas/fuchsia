// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>
#include <memory>

class ThermalCli {
 public:
  ThermalCli(zx::channel channel) : channel_(std::move(channel)) {}

  zx_status_t PrintTemperature();
  zx_status_t FanLevelCommand(const char* value);
  int FrequencyCommand(uint32_t cluster, const char* value);

 private:
  zx::channel channel_;
};
