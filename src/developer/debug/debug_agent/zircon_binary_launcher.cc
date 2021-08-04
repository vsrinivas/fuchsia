// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_binary_launcher.h"

#include <inttypes.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <zircon/processargs.h>

#include "src/developer/debug/debug_agent/zircon_process_handle.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

ZirconBinaryLauncher::ZirconBinaryLauncher(std::shared_ptr<sys::ServiceDirectory> env_services)
    : builder_(env_services) {}
ZirconBinaryLauncher::~ZirconBinaryLauncher() = default;

debug::Status ZirconBinaryLauncher::Setup(const std::vector<std::string>& argv) {
  if (zx_status_t status = builder_.LoadPath(argv[0]); status != ZX_OK) {
    if (status == ZX_ERR_NOT_FOUND) {
      // Rewrite this common error to provide a better message.
      return debug::Status(debug::Status::kNotFound,
                           fxl::StringPrintf("The binary '%s' was not found.", argv[0].c_str()));
    }
    return debug::ZxStatus(status);
  }

  builder_.AddArgs(argv);
  builder_.CloneJob();
  builder_.CloneNamespace();
  builder_.CloneEnvironment();

  stdio_handles_.out = AddStdioEndpoint(STDOUT_FILENO);
  stdio_handles_.err = AddStdioEndpoint(STDERR_FILENO);

  return debug::ZxStatus(builder_.Prepare(nullptr));
}

std::unique_ptr<ProcessHandle> ZirconBinaryLauncher::GetProcess() const {
  zx::process process;
  builder_.data().process.duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  return std::make_unique<ZirconProcessHandle>(std::move(process));
}

debug::Status ZirconBinaryLauncher::Start() { return debug::ZxStatus(builder_.Start(nullptr)); }

zx::socket ZirconBinaryLauncher::AddStdioEndpoint(int fd) {
  zx::socket local;
  zx::socket target;
  zx_status_t status = zx::socket::create(0, &local, &target);
  if (status != ZX_OK)
    return zx::socket();

  builder_.AddHandle(PA_HND(PA_FD, fd), std::move(target));
  return local;
}

StdioHandles ZirconBinaryLauncher::ReleaseStdioHandles() { return std::move(stdio_handles_); }

}  // namespace debug_agent
