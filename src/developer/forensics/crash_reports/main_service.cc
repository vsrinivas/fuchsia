// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/main_service.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/crash_reports/crash_register.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace forensics {
namespace crash_reports {
namespace {

const char kCrashRegisterPath[] = "/tmp/crash_register.json";

ErrorOr<std::string> ReadStringFromFile(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    FX_LOGS(ERROR) << "Failed to read content from " << filepath;
    return Error::kFileReadFailure;
  }
  return std::string(fxl::TrimString(content, "\r\n"));
}

}  // namespace

std::unique_ptr<MainService> MainService::Create(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 timekeeper::Clock* clock,
                                                 std::shared_ptr<InfoContext> info_context,
                                                 Config config) {
  const ErrorOr<std::string> build_version = ReadStringFromFile("/config/build-info/version");

  AnnotationMap default_annotations;

  default_annotations.Set("osName", "Fuchsia")
      .Set("osVersion", build_version)
      // TODO(fxbug.dev/70398): These keys are duplicates from feedback data, find a better way to
      // share them.
      .Set("build.version", build_version)
      .Set("build.board", ReadStringFromFile("/config/build-info/board"))
      .Set("build.product", ReadStringFromFile("/config/build-info/product"))
      .Set("build.latest-commit-date", ReadStringFromFile("/config/build-info/latest-commit-date"));

  std::unique_ptr<CrashRegister> crash_register = std::make_unique<CrashRegister>(
      dispatcher, services, info_context, build_version, kCrashRegisterPath);

  auto crash_reporter = CrashReporter::Create(dispatcher, services, clock, info_context, config,
                                              std::move(default_annotations), crash_register.get());

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
      crash_register_(std::move(crash_register)),
      crash_reporter_(std::move(crash_reporter)) {
  FX_CHECK(crash_register_);
  FX_CHECK(crash_reporter_);

  info_.ExposeConfig(config);
}

void MainService::ShutdownImminent() { crash_reporter_->PersistAllCrashReports(); }

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
