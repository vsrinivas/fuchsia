// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/launcher.h"

#include <inttypes.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <zircon/processargs.h>

#include "src/developer/debug/debug_agent/object_util.h"

namespace debug_agent {

Launcher::Launcher(std::shared_ptr<sys::ServiceDirectory> env_services)
    : builder_(env_services) {}

zx_status_t Launcher::Setup(const std::vector<std::string>& argv) {
  zx_status_t status = builder_.LoadPath(argv[0]);
  if (status != ZX_OK)
    return status;

  builder_.AddArgs(argv);
  builder_.CloneJob();
  builder_.CloneNamespace();
  builder_.CloneEnvironment();

  out_ = AddStdioEndpoint(STDOUT_FILENO);
  err_ = AddStdioEndpoint(STDERR_FILENO);

  return builder_.Prepare(nullptr);
}

zx::process Launcher::GetProcess() const {
  zx::process process;
  builder_.data().process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  return process;
}

zx_status_t Launcher::Start() { return builder_.Start(nullptr); }

zx::socket Launcher::AddStdioEndpoint(int fd) {
  zx::socket local;
  zx::socket target;
  zx_status_t status = zx::socket::create(0, &local, &target);
  if (status != ZX_OK)
    return zx::socket();

  builder_.AddHandle(PA_HND(PA_FD, fd), std::move(target));
  return local;
}

zx::socket Launcher::ReleaseStdout() { return std::move(out_); }
zx::socket Launcher::ReleaseStderr() { return std::move(err_); }

}  // namespace debug_agent
