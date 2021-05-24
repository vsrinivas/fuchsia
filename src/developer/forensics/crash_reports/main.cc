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

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/main_service.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace crash_reports {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/crash_reports/default_config.json";
const char kOverrideConfigPath[] = "/config/data/crash_reports/override_config.json";

std::optional<Config> GetConfig() {
  std::optional<Config> config;
  if (files::IsFile(kOverrideConfigPath)) {
    if (config = ParseConfig(kOverrideConfigPath); !config) {
      FX_LOGS(ERROR) << "Failed to read override config file at " << kOverrideConfigPath
                     << " - falling back to default config file";
    }
  }

  if (!config) {
    if (config = ParseConfig(kDefaultConfigPath); !config) {
      FX_LOGS(ERROR) << "Failed to read default config file at " << kDefaultConfigPath;
    }
  }

  return config;
}

}  // namespace

int main() {
  syslog::SetTags({"forensics", "crash"});

  forensics::component::Component component;

  auto config = GetConfig();
  if (!config) {
    FX_LOGS(FATAL) << "Failed to set up crash reporter";
    return EXIT_FAILURE;
  }

  timekeeper::SystemClock clock;

  auto info_context = std::make_shared<InfoContext>(component.InspectRoot(), &clock,
                                                    component.Dispatcher(), component.Services());

  std::unique_ptr<MainService> main_service =
      MainService::Create(component.Dispatcher(), component.Services(), &clock,
                          std::move(info_context), std::move(*config));

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

  component.OnStopSignal([&](::fit::deferred_callback) {
    FX_LOGS(INFO) << "Received stop signal; stopping upload and snapshot request, but not exiting "
                     "to continue persisting new reports.";
    main_service->ShutdownImminent();
    // Don't stop the loop so incoming crash reports can be persisted while appmgr is waiting to
    // terminate v1 components.
  });

  component.RunLoop();

  return EXIT_SUCCESS;
}

}  // namespace crash_reports
}  // namespace forensics
