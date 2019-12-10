// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_INFO_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_INFO_H_

#include <memory>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/developer/feedback/crashpad_agent/metrics_registry.cb.h"
#include "src/developer/feedback/crashpad_agent/settings.h"

namespace feedback {

// Information about the agent we want to export.
struct AgentInfo {
 public:
  AgentInfo(std::shared_ptr<InfoContext> context);

  // Exposes the static configuration of the agent.
  void ExposeConfig(const feedback::Config& config);

  // Exposes the mutable settings of the agent.
  void ExposeSettings(feedback::Settings* settings);

  void LogCrashState(cobalt_registry::CrashMetricDimensionState state);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_INFO_H_
