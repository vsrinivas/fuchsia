// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
#define GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fxl/macros.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <third_party/crashpad/client/crash_report_database.h>
#include <third_party/crashpad/util/misc/uuid.h>

namespace fuchsia {
namespace crash {

class CrashpadAnalyzerImpl : public Analyzer {
 public:
  CrashpadAnalyzerImpl();

  void Analyze(zx::process process, zx::thread thread, zx::port exception_port,
               AnalyzeCallback callback) override;

  void Process(fuchsia::mem::Buffer crashlog,
               ProcessCallback callback) override;

 private:
  int HandleException(zx::process process, zx::thread thread,
                      zx::port exception_port);
  int Process(fuchsia::mem::Buffer crashlog);

  int UploadReport(
      std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> report,
      const std::map<std::string, std::string>& annotations);
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport>
  GetUploadReport(const crashpad::UUID& local_report_id);

  std::unique_ptr<crashpad::CrashReportDatabase> database_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAnalyzerImpl);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // GARNET_BIN_CRASHPAD_CRASHPAD_ANALYZER_IMPL_H_
