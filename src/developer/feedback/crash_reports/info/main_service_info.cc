// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crash_reports/info/main_service_info.h"

#include <lib/syslog/cpp/macros.h>

namespace feedback {

MainServiceInfo::MainServiceInfo(std::shared_ptr<InfoContext> context)
    : context_(std::move(context)) {
  FX_CHECK(context_);
}

void MainServiceInfo::ExposeConfig(const feedback::Config& config) {
  context_->InspectManager().ExposeConfig(config);
}

void MainServiceInfo::UpdateCrashReporterProtocolStats(InspectProtocolStatsUpdateFn update) {
  context_->InspectManager().UpdateCrashReporterProtocolStats(update);
}

}  // namespace feedback
