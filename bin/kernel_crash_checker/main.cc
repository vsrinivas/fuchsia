// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/logging.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

class CrashpadAnalyzer {
 public:
  explicit CrashpadAnalyzer()
      : context_(component::StartupContext::CreateFromStartupInfo()) {
    FXL_DCHECK(context_);
  }

  void Process(fuchsia::crash::Buffer crashlog) {
    fuchsia::crash::AnalyzerSyncPtr analyzer;
    context_->ConnectToEnvironmentService(analyzer.NewRequest());
    FXL_DCHECK(analyzer);

    analyzer->Process(fbl::move(crashlog));
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
};

int main(int argc, char** argv) {
  const char filepath[] = "/boot/log/last-panic.txt";
  fxl::UniqueFD fd(open(filepath, O_RDONLY));
  if (!fd.is_valid()) {
    FXL_LOG(INFO) << "no kernel crash log found";
    return 0;
  }

  fsl::SizedVmo crashlog_vmo;
  if (!fsl::VmoFromFd(std::move(fd), &crashlog_vmo)) {
    FXL_LOG(ERROR) << "error loading kernel crash log into VMO";
    return 1;
  }

  std::string crashlog_str;
  if (!fsl::StringFromVmo(crashlog_vmo, &crashlog_str)) {
    FXL_LOG(ERROR) << "error converting kernel crash log VMO to string";
    return 1;
  }
  FXL_LOG(INFO) << "dumping log from previous kernel panic:\n" << crashlog_str;

#if USE_CRASHPAD
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  CrashpadAnalyzer crashpad_analyzer;

  // TODO(DX-230): wait for network to come up instead of sleeping for 60s.
  zx_nanosleep(zx_deadline_after(ZX_SEC(60)));

  // Switch to crashlog.ToTransport() once we use fuchsia::mem::Buffer.
  fuchsia::crash::Buffer buf;
  buf.vmo = fbl::move(crashlog_vmo.vmo());
  buf.size = crashlog_vmo.size();
  crashpad_analyzer.Process(fbl::move(buf));
  loop.Run();
#endif  // USE_CRASHPAD

  return 0;
}
