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
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/timekeeper/system_clock.h>

#include <utility>

#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/main_service.h"

int main(int argc, const char** argv) {
  using namespace ::forensics::crash_reports;

  syslog::SetTags({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  const timekeeper::SystemClock clock;

  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspect::Node& root_node = inspector->root();

  auto info_context =
      std::make_shared<InfoContext>(&root_node, clock, loop.dispatcher(), context->svc());

  std::unique_ptr<MainService> main_service =
      MainService::TryCreate(loop.dispatcher(), context->svc(), clock, std::move(info_context));
  if (!main_service) {
    return EXIT_FAILURE;
  }

  // fuchsia.feedback.CrashReporter
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter>(
          [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
            main_service->HandleCrashReporterRequest(std::move(request));
          }));
  // fuchsia.feedback.CrashReportingProductRegister
  context->outgoing()->AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReportingProductRegister>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request) {
            main_service->HandleCrashRegisterRequest(std::move(request));
          }));

  loop.Run();

  return EXIT_SUCCESS;
}
