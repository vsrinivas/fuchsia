// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/main_service.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/crash_register.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace crash_reports {
namespace {

const char kCrashRegisterPath[] = "/tmp/crash_register.json";

}  // namespace

MainService::MainService(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services,
                         inspect::Node* inspect_root, timekeeper::Clock* clock, Config config,
                         ErrorOr<std::string> build_version, AnnotationMap default_annotations)
    : dispatcher_(dispatcher),
      info_context_(std::make_shared<InfoContext>(inspect_root, clock, dispatcher_, services)),
      info_(info_context_),
      tags_(),
      crash_server_(dispatcher, services, kCrashServerUrl, &tags_),
      snapshot_manager_(dispatcher, services, clock, kSnapshotSharedRequestWindow,
                        kGarbageCollectedSnapshotsPath, kSnapshotAnnotationsMaxSize,
                        kSnapshotArchivesMaxSize),
      crash_register_(dispatcher, services, info_context_, build_version, kCrashRegisterPath),
      crash_reporter_(dispatcher, services, clock, info_context_, config, default_annotations,
                      &crash_register_, &tags_, &snapshot_manager_, &crash_server_) {
  info_.ExposeConfig(config);
}

void MainService::ShutdownImminent() { crash_reporter_.PersistAllCrashReports(); }

void MainService::HandleCrashRegisterRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request) {
  crash_register_connections_.AddBinding(
      &crash_register_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        info_.UpdateCrashRegisterProtocolStats(&InspectProtocolStats::CloseConnection);
      });
  info_.UpdateCrashRegisterProtocolStats(&InspectProtocolStats::NewConnection);
}

void MainService::HandleCrashReporterRequest(
    ::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
  crash_reporter_connections_.AddBinding(
      &crash_reporter_, std::move(request), dispatcher_, [this](const zx_status_t status) {
        info_.UpdateCrashReporterProtocolStats(&InspectProtocolStats::CloseConnection);
      });
  info_.UpdateCrashReporterProtocolStats(&InspectProtocolStats::NewConnection);
}

}  // namespace crash_reports
}  // namespace forensics
