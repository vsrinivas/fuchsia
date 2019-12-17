// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/agent_info.h"

#include "src/developer/feedback/crashpad_agent/metrics_registry.cb.h"
#include "src/lib/fxl/logging.h"

using cobalt_registry::kCrashMetricId;

using CrashState = cobalt_registry::CrashMetricDimensionState;

namespace feedback {

AgentInfo::AgentInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FXL_CHECK(context);
}

void AgentInfo::ExposeConfig(const feedback::Config& config) {
  context_->InspectManager().ExposeConfig(config);
}

void AgentInfo::ExposeSettings(feedback::Settings* settings) {
  context_->InspectManager().ExposeSettings(settings);
}

void AgentInfo::LogCrashState(CrashState state) {
  context_->Cobalt().LogOccurrence(kCrashMetricId, state);
}

}  // namespace feedback
