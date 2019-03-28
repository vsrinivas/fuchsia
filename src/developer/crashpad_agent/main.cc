// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include "src/developer/crashpad_agent/crashpad_agent.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"crash"});

  std::unique_ptr<fuchsia::crash::CrashpadAgent> agent =
      fuchsia::crash::CrashpadAgent::TryCreate();
  if (!agent) {
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = sys::ComponentContext::Create();
  fidl::BindingSet<fuchsia::crash::Analyzer> bindings;
  startup_context->outgoing()->AddPublicService(
      bindings.GetHandler(agent.get()));

  loop.Run();

  return EXIT_SUCCESS;
}
