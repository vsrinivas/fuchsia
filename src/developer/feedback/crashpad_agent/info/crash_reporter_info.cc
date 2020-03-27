// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/crash_reporter_info.h"

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

CrashReporterInfo::CrashReporterInfo(std::shared_ptr<InfoContext> context)
    : context_(std::move(context)) {
  FX_CHECK(context_);
}

void CrashReporterInfo::ExposeSettings(feedback::Settings* settings) {
  context_->InspectManager().ExposeSettings(settings);
}

void CrashReporterInfo::LogCrashState(CrashState state) { context_->Cobalt().LogOccurrence(state); }

}  // namespace feedback
