// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_INFO_CONTEXT_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_INFO_CONTEXT_H_

#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/timekeeper/clock.h>

#include <memory>

#include "src/developer/feedback/crashpad_agent/info/inspect_manager.h"
#include "src/developer/feedback/utils/cobalt.h"

namespace feedback {

// Holds the objects needed to expose information for a component.
class InfoContext {
 public:
  InfoContext(inspect::Node *root_node, timekeeper::Clock *clock,
              std::shared_ptr<sys::ServiceDirectory> services)
      : inspect_manager_(root_node, clock), cobalt_(services) {}
  InspectManager &InspectManager() { return inspect_manager_; }
  Cobalt &Cobalt() { return cobalt_; }

 private:
  class InspectManager inspect_manager_;
  class Cobalt cobalt_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_INFO_CONTEXT_H_
