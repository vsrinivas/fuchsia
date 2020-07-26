// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/testing/fakes/crash_reporter.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"forensics", "test"});

  FX_LOGS(INFO) << "Starting FakeCrashReporter";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  ::forensics::fakes::CrashReporter crash_reporter;

  ::fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_bindings;
  context->outgoing()->AddPublicService(crash_reporter_bindings.GetHandler(&crash_reporter));

  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::testing::FakeCrashReporterQuerier>(
          [&crash_reporter](
              ::fidl::InterfaceRequest<fuchsia::feedback::testing::FakeCrashReporterQuerier>
                  request) { crash_reporter.SetQuerier(std::move(request)); }));

  loop.Run();

  return EXIT_SUCCESS;
}
