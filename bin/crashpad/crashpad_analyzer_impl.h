// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
#define GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_

#include <string>
#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/macros.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <third_party/crashpad/client/crash_report_database.h>
#include <third_party/crashpad/util/misc/uuid.h>
#include <zircon/status.h>

namespace fuchsia {
namespace crash {

class CrashpadAnalyzerImpl : public Analyzer {
 public:
  // Static factory method.
  // Returns nullptr if the analyzer cannot be instantiated, e.g., because the
  // local report database cannot be accessed.
  static std::unique_ptr<CrashpadAnalyzerImpl> TryCreate();

  void HandleNativeException(zx::process process, zx::thread thread,
                             zx::port exception_port,
                             HandleNativeExceptionCallback callback) override;

  void HandleManagedRuntimeException(
      ManagedRuntimeLanguage language, fidl::StringPtr component_url,
      fidl::StringPtr exception, fuchsia::mem::Buffer stackTrace,
      HandleManagedRuntimeExceptionCallback callback) override;

  void ProcessKernelPanicCrashlog(
      fuchsia::mem::Buffer crashlog,
      ProcessKernelPanicCrashlogCallback callback) override;

 private:
  explicit CrashpadAnalyzerImpl(
      std::unique_ptr<crashpad::CrashReportDatabase> database);

  zx_status_t HandleNativeException(zx::process process, zx::thread thread,
                                    zx::port exception_port);
  zx_status_t ProcessKernelPanicCrashlog(fuchsia::mem::Buffer crashlog);

  zx_status_t UploadReport(
      std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> report,
      const std::map<std::string, std::string>& annotations);
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport>
  GetUploadReport(const crashpad::UUID& local_report_id);

  const std::unique_ptr<crashpad::CrashReportDatabase> database_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAnalyzerImpl);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
