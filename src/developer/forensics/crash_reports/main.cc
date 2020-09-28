// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/timekeeper/system_clock.h>

#include <utility>

#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/main_service.h"
#include "src/developer/forensics/utils/component/component.h"

namespace forensics {
namespace crash_reports {

int main() {
  syslog::SetTags({"forensics", "crash"});

  forensics::component::Component component;

  timekeeper::SystemClock clock;

  auto info_context = std::make_shared<InfoContext>(component.InspectRoot(), &clock,
                                                    component.Dispatcher(), component.Services());

  std::unique_ptr<MainService> main_service = MainService::TryCreate(
      component.Dispatcher(), component.Services(), &clock, std::move(info_context));
  if (!main_service) {
    return EXIT_FAILURE;
  }

  // fuchsia.feedback.CrashReporter
  component.AddPublicService(::fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter>(
      [&main_service](::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
        main_service->HandleCrashReporterRequest(std::move(request));
      }));
  // fuchsia.feedback.CrashReportingProductRegister
  component.AddPublicService(
      ::fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReportingProductRegister>(
          [&main_service](
              ::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request) {
            main_service->HandleCrashRegisterRequest(std::move(request));
          }));

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace crash_reports
}  // namespace forensics
