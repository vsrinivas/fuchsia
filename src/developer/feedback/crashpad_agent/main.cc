// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/timekeeper/system_clock.h>

#include <utility>

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspect::Node& root_node = inspector->root();
  timekeeper::SystemClock clock;
  feedback::InspectManager inspect_manager(&root_node, &clock);

  std::unique_ptr<feedback::CrashpadAgent> agent =
      feedback::CrashpadAgent::TryCreate(loop.dispatcher(), context->svc(), &inspect_manager);
  if (!agent) {
    return EXIT_FAILURE;
  }

  fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_bindings;
  context->outgoing()->AddPublicService(crash_reporter_bindings.GetHandler(agent.get()));

  loop.Run();

  return EXIT_SUCCESS;
}
