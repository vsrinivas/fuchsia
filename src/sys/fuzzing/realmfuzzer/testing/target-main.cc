// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple program that reads an exit code from a channel and exits.

#include "src/sys/fuzzing/realmfuzzer/testing/target-main.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace fuzzing {

zx_status_t RunTestTarget() {
  // Take start up handles.
  zx::channel channel(zx_take_startup_handle(PA_HND(PA_USER0, kTestChannelId)));

  // Wait to read how this process should exit.
  auto status =
      channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
  FX_CHECK(status == ZX_OK) << zx_status_get_string(status);

  zx_status_t exitcode = 0;
  status = channel.read(0, &exitcode, nullptr, sizeof(exitcode), 0, nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << zx_status_get_string(status);
  return exitcode;
}

}  // namespace fuzzing

int main() { return fuzzing::RunTestTarget(); }
