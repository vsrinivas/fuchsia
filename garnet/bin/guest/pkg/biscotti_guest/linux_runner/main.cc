// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/logging.h>
#include <trace-provider/provider.h>

#include "garnet/bin/guest/pkg/biscotti_guest/linux_runner/linux_runner.h"

void PrintUsage();

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  linux_runner::LinuxRunner runner;
  zx_status_t status = runner.Init();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start guest: " << status;
    return -1;
  }
  loop.Run();
  return 0;
}
