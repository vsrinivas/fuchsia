// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect_deprecated/component.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include <utility>

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"crash"});

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();

  auto inspector = ::inspect_deprecated::ComponentInspector::Initialize(context.get());
  ::inspect_deprecated::Node& root_node = inspector->root_tree()->GetRoot();
  fuchsia::crash::InspectManager inspect_manager(&root_node);

  std::unique_ptr<fuchsia::crash::CrashpadAgent> agent =
      fuchsia::crash::CrashpadAgent::TryCreate(loop.dispatcher(), context->svc(), &inspect_manager);
  if (!agent) {
    return EXIT_FAILURE;
  }

  // TODO(DX-1820): delete once migrated to fuchsia.feedback.CrashReporter.
  fidl::BindingSet<fuchsia::crash::Analyzer> crash_analyzer_bindings;
  context->outgoing()->AddPublicService(crash_analyzer_bindings.GetHandler(agent.get()));

  fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_bindings;
  context->outgoing()->AddPublicService(crash_reporter_bindings.GetHandler(agent.get()));

  loop.Run();

  return EXIT_SUCCESS;
}
