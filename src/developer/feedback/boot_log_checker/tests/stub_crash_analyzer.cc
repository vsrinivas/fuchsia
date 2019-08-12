// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/boot_log_checker/tests/stub_crash_analyzer.h"

#include <lib/fsl/vmo/strings.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

namespace feedback {

void StubCrashAnalyzer::OnKernelPanicCrashLog(fuchsia::mem::Buffer crash_log,
                                              OnKernelPanicCrashLogCallback callback) {
  fuchsia::crash::Analyzer_OnKernelPanicCrashLog_Result result;
  if (!fsl::StringFromVmo(crash_log, &kernel_panic_crash_log_)) {
    FX_LOGS(ERROR) << "error parsing crash log VMO as string";
    result.set_err(ZX_ERR_INTERNAL);
  } else {
    fuchsia::crash::Analyzer_OnKernelPanicCrashLog_Response response;
    result.set_response(response);
  }
  callback(std::move(result));
}

void StubCrashAnalyzerAlwaysReturnsError::OnKernelPanicCrashLog(
    fuchsia::mem::Buffer crash_log, OnKernelPanicCrashLogCallback callback) {
  fuchsia::crash::Analyzer_OnKernelPanicCrashLog_Result result;
  result.set_err(ZX_ERR_INTERNAL);
  callback(std::move(result));
}

}  // namespace feedback
