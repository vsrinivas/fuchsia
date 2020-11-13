// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/crash_reporter_info.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

CrashReporterInfo::CrashReporterInfo(std::shared_ptr<InfoContext> context)
    : context_(std::move(context)) {
  FX_CHECK(context_);
}

void CrashReporterInfo::ExposeReportingPolicy(ReportingPolicyWatcher* watcher) {
  context_->InspectManager().ExposeReportingPolicy(watcher);
}

void CrashReporterInfo::LogCrashState(cobalt::CrashState state) {
  context_->Cobalt().LogOccurrence(state);
}

}  // namespace crash_reports
}  // namespace forensics
