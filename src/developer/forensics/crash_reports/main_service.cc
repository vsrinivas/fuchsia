// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/main_service.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/crash_register.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics {
namespace crash_reports {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/crash_reports/default_config.json";
const char kOverrideConfigPath[] = "/config/data/crash_reports/override_config.json";
const char kCrashRegisterPath[] = "/tmp/crash_register.json";

}  // namespace

std::unique_ptr<MainService> MainService::TryCreate(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    const timekeeper::Clock& clock,
                                                    std::shared_ptr<InfoContext> info_context) {
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

      FX_LOGS(FATAL) << "Failed to set up main service";
      return nullptr;
    }
  }

  return MainService::TryCreate(dispatcher, std::move(services), clock, std::move(info_context),
                                std::move(config));
}

namespace {

ErrorOr<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from " << filepath;
    return Error::kFileReadFailure;
  }
  return std::string(fxl::TrimString(content, "\r\n"));
}

}  // namespace

std::unique_ptr<MainService> MainService::TryCreate(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    const timekeeper::Clock& clock,
                                                    std::shared_ptr<InfoContext> info_context,
                                                    Config config) {
  const ErrorOr<std::string> build_version = ReadStringFromFile("/config/build-info/version");

  std::unique_ptr<CrashRegister> crash_register = std::make_unique<CrashRegister>(
      dispatcher, services, info_context, build_version, kCrashRegisterPath);

  auto crash_reporter = CrashReporter::TryCreate(dispatcher, services, clock, info_context, &config,
                                                 build_version, crash_register.get());
  if (!crash_reporter) {
    FX_LOGS(FATAL) << "Failed to set up main service";
    return nullptr;
  }

  return std::unique_ptr<MainService>(new MainService(
      dispatcher, std::move(services), std::move(info_context), std::move(config),
      std::move(build_version), std::move(crash_register), std::move(crash_reporter)));
}

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         std::shared_ptr<InfoContext> info_context, Config config,
                         const ErrorOr<std::string>& build_version,
                         std::unique_ptr<CrashRegister> crash_register,
                         std::unique_ptr<CrashReporter> crash_reporter)
    : dispatcher_(dispatcher),
      info_(info_context),
      config_(std::move(config)),
      crash_register_(std::move(crash_register)),
      crash_reporter_(std::move(crash_reporter)) {
  FX_CHECK(crash_register_);
  FX_CHECK(crash_reporter_);

  info_.ExposeConfig(config_);
}

void MainService::HandleCrashRegisterRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request) {
  crash_register_connections_.AddBinding(
      crash_register_.get(), std::move(request), dispatcher_, [this](const zx_status_t status) {
        info_.UpdateCrashRegisterProtocolStats(&InspectProtocolStats::CloseConnection);
      });
  info_.UpdateCrashRegisterProtocolStats(&InspectProtocolStats::NewConnection);
}

void MainService::HandleCrashReporterRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
  crash_reporter_connections_.AddBinding(
      crash_reporter_.get(), std::move(request), dispatcher_, [this](const zx_status_t status) {
        info_.UpdateCrashReporterProtocolStats(&InspectProtocolStats::CloseConnection);
      });
  info_.UpdateCrashReporterProtocolStats(&InspectProtocolStats::NewConnection);
}

}  // namespace crash_reports
}  // namespace forensics
