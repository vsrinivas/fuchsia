// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/virtualization/bin/linux_runner/linux_runner.h"

void PrintUsage();

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  linux_runner::LinuxRunner runner(loop.dispatcher());
  loop.Run();
  return 0;
}
