// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_CRASH_REPORTER_INFO_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_CRASH_REPORTER_INFO_H_

#include <memory>

#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace crash_reports {

// Information about the crash reporter we want to export.
struct CrashReporterInfo {
 public:
  CrashReporterInfo(std::shared_ptr<InfoContext> context);

  // Exposes the reporting policy of the crash reporter.
  void ExposeReportingPolicy(ReportingPolicyWatcher* watcher);

  void LogCrashState(cobalt::CrashState state);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_CRASH_REPORTER_INFO_H_
