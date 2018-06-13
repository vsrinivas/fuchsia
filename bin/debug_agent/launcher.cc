// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/launcher.h"

namespace debug_agent {

zx_status_t Launcher::Setup(const std::vector<std::string>& argv) {
  zx_status_t status = builder_.LoadPath(argv[0]);
  if (status != ZX_OK)
    return status;

  if (argv.size() > 1) {
    builder_.AddArgs(std::vector<std::string>(argv.begin() + 1, argv.end()));
  } else {
    builder_.SetName(argv[0]);
  }

  /*
  Transfering STDIO handles is currently disabled. When doing local debugging
  sharing stdio currently leaves the debugger UI in an inconsistent state and
  stdout doesn't work. Instead we need to redirect stdio in a way the debugger
  can control.

  builder_.CloneStdio();
  */
  builder_.CloneJob();
  builder_.CloneLdsvc();
  builder_.CloneNamespace();
  builder_.CloneEnvironment();

  return builder_.Prepare(nullptr);
}

zx::process Launcher::GetProcess() const {
  zx::process process;
  builder_.data().process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  return process;
}

zx_status_t Launcher::Start() { return builder_.Start(nullptr); }

}  // namespace debug_agent
