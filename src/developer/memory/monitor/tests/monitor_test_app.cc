// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/developer/memory/monitor/monitor.h"
#include "src/lib/fxl/command_line.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  monitor::Monitor app(sys::ComponentContext::Create(), fxl::CommandLine{}, loop.dispatcher(),
                       false, false);
  loop.Run();
  return 0;
}
