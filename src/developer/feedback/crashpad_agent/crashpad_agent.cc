// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

}  // namespace

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context) {
  Config config;

  // We use the default config included in the package of this component if no override config was
  // specified or if we failed to parse the override config.
  bool use_default_config = true;

  if (files::IsFile(kOverrideConfigPath)) {
    use_default_config = false;
    if (const zx_status_t status = ParseConfig(kOverrideConfigPath, &config); status != ZX_OK) {
      // We failed to parse the override config: fall back to the default config.
      use_default_config = true;
      FX_PLOGS(ERROR, status) << "Failed to read override config file at " << kOverrideConfigPath
                              << " - falling back to default config file";
    }
  }

  // Either there was no override config or we failed to parse it.
  if (use_default_config) {
    if (const zx_status_t status = ParseConfig(kDefaultConfigPath, &config); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to read default config file at " << kDefaultConfigPath;

      FX_LOGS(FATAL) << "Failed to set up agent";
      return nullptr;
    }
  }

  return CrashpadAgent::TryCreate(dispatcher, std::move(services), clock, std::move(info_context),
                                  std::move(config));
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context, Config config) {
  auto crash_reporter =
      CrashReporter::TryCreate(dispatcher, services, clock, info_context, &config);
  if (!crash_reporter) {
    FX_LOGS(FATAL) << "Failed to set up agent";
    return nullptr;
  }

  return std::unique_ptr<CrashpadAgent>(
      new CrashpadAgent(dispatcher, std::move(services), std::move(info_context), std::move(config),
                        std::move(crash_reporter)));
}

CrashpadAgent::CrashpadAgent(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services,
                             std::shared_ptr<InfoContext> info_context, Config config,
                             std::unique_ptr<CrashReporter> crash_reporter)
    : dispatcher_(dispatcher),
      info_(std::move(info_context)),
      config_(std::move(config)),
      crash_reporter_(std::move(crash_reporter)) {
  FX_CHECK(crash_reporter_);

  info_.ExposeConfig(config_);
}

void CrashpadAgent::HandleCrashReporterRequest(
    fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
  crash_reporter_connections_.AddBinding(crash_reporter_.get(), std::move(request), dispatcher_);
}

}  // namespace feedback
