// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_BIN_LIGHTS_CLI_LIGHTS_CLI_H_
#define SRC_UI_LIGHT_BIN_LIGHTS_CLI_LIGHTS_CLI_H_

#include <fuchsia/hardware/light/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <memory>

class LightsCli {
 public:
  explicit LightsCli(zx::channel channel)
      : client_(fidl::ClientEnd<fuchsia_hardware_light::Light>(std::move(channel))) {}
  zx_status_t PrintValue(uint32_t idx);
  zx_status_t SetValue(uint32_t idx, double value);
  zx_status_t Summary();

 private:
  fidl::WireSyncClient<fuchsia_hardware_light::Light> client_;
};

#endif  // SRC_UI_LIGHT_BIN_LIGHTS_CLI_LIGHTS_CLI_H_
