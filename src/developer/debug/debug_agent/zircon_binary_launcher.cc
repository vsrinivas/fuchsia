// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_binary_launcher.h"

#include <inttypes.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <zircon/processargs.h>

#include "src/developer/debug/debug_agent/zircon_process_handle.h"

namespace debug_agent {

ZirconBinaryLauncher::ZirconBinaryLauncher(std::shared_ptr<sys::ServiceDirectory> env_services)
    : builder_(env_services) {}
ZirconBinaryLauncher::~ZirconBinaryLauncher() = default;

debug::Status ZirconBinaryLauncher::Setup(const std::vector<std::string>& argv) {
  // TODO(fxbug.dev/81801) ProcessBuilder::LoadPath currently hangs forever for any binaries not
  // in our package (which makes this code path useless for normal debugging). This does work for
  // test binaries we package, but currently these tests are disabled so we can report the error
  // here and avoid the hang in production.
  //
  // We need to evaluate whether this is a feature that can be supported on Fuchsia, and then
  // possibly remove this entire class. For now, just return failure to avoid a hang.
  return debug::Status(
      debug::Status::kNotSupported,
      "Directly launching binaries (not as components) is not currently supported.\n"
      "See fxbug.dev/81801");
#if 0
  if (zx_status_t status = builder_.LoadPath(argv[0]); status != ZX_OK)
    return debug::ZxStatus(status);

  builder_.AddArgs(argv);
  builder_.CloneJob();
  builder_.CloneNamespace();
  builder_.CloneEnvironment();

  stdio_handles_.out = AddStdioEndpoint(STDOUT_FILENO);
  stdio_handles_.err = AddStdioEndpoint(STDERR_FILENO);

  return debug::ZxStatus(builder_.Prepare(nullptr));
#endif
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
