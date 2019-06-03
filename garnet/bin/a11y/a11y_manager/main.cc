// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

#include "garnet/bin/a11y/a11y_manager/app.h"
#include "lib/syslog/cpp/logger.h"

int main(int argc, const char** argv) {
  syslog::InitLogger();
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  a11y_manager::App app;
  loop.Run();
  return 0;
}
