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
#include <fuchsia/net/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/logging.h>
#include <lib/syslog/cpp/logger.h>
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

  void ProcessCrashlog(fuchsia::mem::Buffer crashlog) {
    fuchsia::crash::AnalyzerSyncPtr analyzer;
    context_->ConnectToEnvironmentService(analyzer.NewRequest());
    FXL_DCHECK(analyzer);

    const zx_status_t status = analyzer->ProcessCrashlog(fbl::move(crashlog));
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to connect to crash analyzer: " << status
                     << " (" << zx_status_get_string(status) << ")";
    }
  }

 private:
  std::unique_ptr<component::StartupContext> context_;
};

int main(int argc, char** argv) {
  syslog::InitLogger({"crash"});

  const char filepath[] = "/boot/log/last-panic.txt";
  fxl::UniqueFD fd(open(filepath, O_RDONLY));
  if (!fd.is_valid()) {
    FX_LOGS(INFO) << "no kernel crash log found";
    return 0;
  }

  fsl::SizedVmo crashlog_vmo;
  if (!fsl::VmoFromFd(std::move(fd), &crashlog_vmo)) {
    FX_LOGS(ERROR) << "error loading kernel crash log into VMO";
    return 1;
  }

  std::string crashlog_str;
  if (!fsl::StringFromVmo(crashlog_vmo, &crashlog_str)) {
    FX_LOGS(ERROR) << "error converting kernel crash log VMO to string";
    return 1;
  }
  FX_LOGS(INFO) << "dumping log from previous kernel panic:\n" << crashlog_str;

#if USE_CRASHPAD
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  fuchsia::net::ConnectivityPtr connectivity =
      component::StartupContext::CreateFromStartupInfo()
          ->ConnectToEnvironmentService<fuchsia::net::Connectivity>();
  connectivity.events().OnNetworkReachable = [&crashlog_vmo](bool reachable) {
    if (!reachable) {
      return;
    }
    CrashpadAnalyzer crashpad_analyzer;
    crashpad_analyzer.ProcessCrashlog(std::move(crashlog_vmo).ToTransport());
  };
  loop.Run();
#endif  // USE_CRASHPAD

  return 0;
}
