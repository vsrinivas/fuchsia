// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/crash_reports.h"

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/feedback/constants.h"

namespace forensics::feedback {

CrashReports::CrashReports(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services,
                           timekeeper::Clock* clock, inspect::Node* inspect_root,
                           feedback::AnnotationManager* annotation_manager,
                           feedback_data::DataProviderInternal* data_provider,
                           const Options options)
    : dispatcher_(dispatcher),
      info_context_(
          std::make_shared<crash_reports::InfoContext>(inspect_root, clock, dispatcher, services)),
      tags_(),
      crash_server_(dispatcher, services, kCrashServerUrl, &tags_),
      report_store_(
          &tags_, info_context_, annotation_manager,
          /*temp_reports_root=*/
          crash_reports::ReportStore::Root{crash_reports::kReportStoreTmpPath,
                                           crash_reports::kReportStoreMaxTmpSize},
          /*persistent_reports_root=*/
          crash_reports::ReportStore::Root{crash_reports::kReportStoreCachePath,
                                           crash_reports::kReportStoreMaxCacheSize},
          /*temp_snapshots_root=*/
          crash_reports::SnapshotPersistence::Root{crash_reports::kSnapshotStoreTmpPath,
                                                   crash_reports::kSnapshotStoreMaxTmpSize},
          /*persistent_snapshots_root=*/
          crash_reports::SnapshotPersistence::Root{crash_reports::kSnapshotStoreCachePath,
                                                   crash_reports::kSnapshotStoreMaxCacheSize},
          kGarbageCollectedSnapshotsPath, options.snapshot_store_max_archives_size),
      crash_register_(info_context_, kCrashRegisterPath),
      crash_reporter_(dispatcher, services, clock, info_context_, options.config, &crash_register_,
                      &tags_, &crash_server_, &report_store_, data_provider,
                      options.snapshot_collector_window_duration),
      info_(info_context_) {
  info_.ExposeConfig(options.config);
}

void CrashReports::Handle(::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request,
                          ::fit::function<void(zx_status_t)> error_handler) {
  crash_reporter_connections_.AddBinding(&crash_reporter_, std::move(request), dispatcher_,
                                         std::move(error_handler));
}

void CrashReports::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  crash_reporting_product_register_connections_.AddBinding(&crash_register_, std::move(request),
                                                           dispatcher_, std::move(error_handler));
}

fuchsia::feedback::CrashReporter* CrashReports::CrashReporter() { return &crash_reporter_; }

void CrashReports::ShutdownImminent() { crash_reporter_.PersistAllCrashReports(); }

}  // namespace forensics::feedback
