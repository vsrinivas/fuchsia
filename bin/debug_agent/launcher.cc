// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/launcher.h"

#include <inttypes.h>

#include "garnet/bin/debug_agent/object_util.h"

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

  status = builder_.Prepare(nullptr);
  if (status != ZX_OK)
    return status;

  // Setting this property before startup will signal to the loader to debug
  // break once the DEBUG_ADDR property is set properly.
  const intptr_t kMagicValue = ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET;
  return builder_.data().process.set_property(
      ZX_PROP_PROCESS_DEBUG_ADDR, &kMagicValue, sizeof(kMagicValue));
}

zx::process Launcher::GetProcess() const {
  zx::process process;
  builder_.data().process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  return process;
}

zx_status_t Launcher::Start() { return builder_.Start(nullptr); }

}  // namespace debug_agent
