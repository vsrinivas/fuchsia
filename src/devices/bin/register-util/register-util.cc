// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "register-util.h"

#include <fcntl.h>
#include <fuchsia/hardware/registers/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/status.h>

int run(int argc, const char** argv, zx::channel channel) {
  llcpp::fuchsia::hardware::registers::Device::SyncClient device(std::move(channel));
  size_t address = strtoul(argv[2], nullptr, 16);
  uint32_t value = static_cast<uint32_t>(strtoul(argv[3], nullptr, 16));
  zx_status_t status = ZX_OK;
  auto result = device.WriteRegister(address, value);
  if (result->result.is_err()) {
    status = result->result.err();
  }
  if (status != ZX_OK) {
    fprintf(stderr, "Write failed due to error %s\n", zx_status_get_string(status));
  }
  return status;
}
