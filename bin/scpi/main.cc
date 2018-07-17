// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"

#include <lib/async-loop/cpp/loop.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  scpi::App app;
  zx_status_t status = app.Start();
  if (status != ZX_OK) {
    return -1;
  }

  loop.Run();
  return 0;
}
