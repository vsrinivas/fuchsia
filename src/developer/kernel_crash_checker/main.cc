// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/net/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <string>

#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"

class CrashAnalyzer {
 public:
  CrashAnalyzer() : context_(sys::ComponentContext::Create()) {
    FXL_DCHECK(context_);
  }

  void ProcessCrashlog(fuchsia::mem::Buffer crashlog) {
    fuchsia::crash::AnalyzerSyncPtr analyzer;
    context_->svc()->Connect(analyzer.NewRequest());
    FXL_DCHECK(analyzer);

    fuchsia::crash::Analyzer_OnKernelPanicCrashLog_Result out_result;
    const zx_status_t status =
        analyzer->OnKernelPanicCrashLog(std::move(crashlog), &out_result);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "failed to connect to crash analyzer";
    } else if (out_result.is_err()) {
      FX_PLOGS(ERROR, out_result.err())
          << "failed to process kernel panic crash log";
    }
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
};

int main(int argc, char** argv) {
  syslog::InitLogger({"crash"});

  const char filepath[] = "/boot/log/last-panic.txt";
  fxl::UniqueFD fd(open(filepath, O_RDONLY));
  if (!fd.is_valid()) {
    FX_LOGS(INFO) << "no kernel crash log found";
    return EXIT_SUCCESS;
  }

  fsl::SizedVmo crashlog_vmo;
  if (!fsl::VmoFromFd(std::move(fd), &crashlog_vmo)) {
    FX_LOGS(ERROR) << "error loading kernel crash log into VMO";
    return EXIT_FAILURE;
  }

  std::string crashlog_str;
  if (!fsl::StringFromVmo(crashlog_vmo, &crashlog_str)) {
    FX_LOGS(ERROR) << "error converting kernel crash log VMO to string";
    return EXIT_FAILURE;
  }
  FX_LOGS(INFO) << "dumping log from previous kernel panic:\n" << crashlog_str;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  fuchsia::net::ConnectivityPtr connectivity =
      sys::ComponentContext::Create()
          ->svc()
          ->Connect<fuchsia::net::Connectivity>();
  connectivity.events().OnNetworkReachable = [&crashlog_vmo](bool reachable) {
    if (!reachable) {
      return;
    }
    CrashAnalyzer crash_analyzer;
    crash_analyzer.ProcessCrashlog(std::move(crashlog_vmo).ToTransport());
  };
  loop.Run();

  return EXIT_SUCCESS;
}
