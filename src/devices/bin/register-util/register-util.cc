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
  fidl::WireSyncClient<fuchsia_hardware_registers::Device> device(std::move(channel));
  size_t address = strtoul(argv[2], nullptr, 16);
  zx_status_t status = ZX_OK;
  if (argc == 4) {
    uint32_t value = static_cast<uint32_t>(strtoul(argv[3], nullptr, 16));
    auto result = device.WriteRegister32(address, 0xFFFFFFFF, value);
    if (result->result.is_err()) {
      status = result->result.err();
    }
    if (status != ZX_OK) {
      fprintf(stderr, "Write failed due to error %s\n", zx_status_get_string(status));
    }
  } else if (argc == 3) {
    auto result = device.ReadRegister32(address, 0xFFFFFFFF);
    if (result->result.is_err()) {
      status = result->result.err();
    }
    if (status != ZX_OK) {
      fprintf(stderr, "Read failed due to error %s\n", zx_status_get_string(status));
    } else {
      printf("Register 0x%08zx: 0x%08x", address, result->result.response().value);
    }
  } else {
    fprintf(stderr, "Invalid args\n");
    status = ZX_ERR_NOT_SUPPORTED;
  }
  return status;
}
