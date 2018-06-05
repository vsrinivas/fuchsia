// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/appmgr.h"
#include "lib/fxl/command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto request = zx_get_startup_handle(PA_DIRECTORY_REQUEST);

  fuchsia::sys::Appmgr appmgr(loop.async(), std::move(request));

  loop.Run();
  return 0;
}
