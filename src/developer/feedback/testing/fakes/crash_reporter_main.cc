// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/developer/feedback/testing/fakes/crash_reporter.h"
#include "src/lib/syslog/cpp/logger.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback", "test"});

  FX_LOGS(INFO) << "Starting FakeCrashReporter";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  feedback::fakes::CrashReporter crash_reporter;

  fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_bindings;
  context->outgoing()->AddPublicService(crash_reporter_bindings.GetHandler(&crash_reporter));

  loop.Run();

  return EXIT_SUCCESS;
}
