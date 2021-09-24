// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple program that reads an exit code from a channel and exits.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

int main(int argc, char** argv) {
  zx::channel channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  int exitcode = 0;
  auto status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                 zx::deadline_after(zx::duration::infinite()), nullptr);
  FX_CHECK(status == ZX_OK);
  status = channel.read(0, &exitcode, nullptr, sizeof(exitcode), 0, nullptr, nullptr);
  FX_CHECK(status == ZX_OK);
  return exitcode;
}
