// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/agent_info.h"

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

AgentInfo::AgentInfo(std::shared_ptr<InfoContext> context) : context_(std::move(context)) {
  FX_CHECK(context_);
}

void AgentInfo::ExposeConfig(const feedback::Config& config) {
  context_->InspectManager().ExposeConfig(config);
}

}  // namespace feedback
