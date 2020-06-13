// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include <fbl/unique_fd.h>

#include "gpioutil.h"

int main(int argc, char** argv) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return -1;
  }
  status = fdio_service_connect(argv[1], remote.release());
  if (status != ZX_OK) {
    return -1;
  }

  GpioFunc func;
  uint8_t write_value, out_value;
  ::llcpp::fuchsia::hardware::gpio::GpioFlags in_flag;
  if (ParseArgs(argc, argv, &func, &write_value, &in_flag, &out_value)) {
    return -1;
  }

  return ClientCall(::llcpp::fuchsia::hardware::gpio::Gpio::SyncClient(std::move(local)), func,
                    write_value, in_flag, out_value);
}
