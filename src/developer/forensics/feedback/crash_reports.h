// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_CRASH_REPORTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_CRASH_REPORTS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/crash_register.h"
#include "src/developer/forensics/crash_reports/crash_reporter.h"
#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/info/main_service_info.h"
#include "src/developer/forensics/crash_reports/log_tags.h"
#include "src/developer/forensics/crash_reports/report_store.h"
#include "src/developer/forensics/crash_reports/snapshot_collector.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback_data/data_provider.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics::feedback {

class CrashReports {
 public:
  struct Options {
    crash_reports::Config config;
    StorageSize snapshot_store_max_archives_size;
    zx::duration snapshot_collector_window_duration;
  };

  CrashReports(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               timekeeper::Clock* clock, inspect::Node* inspect_root,
               feedback::AnnotationManager* annotation_manager,
               feedback_data::DataProviderInternal* data_provider, Options options);

  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request,
              ::fit::function<void(zx_status_t)> error_handler);
  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::CrashReportingProductRegister> request,
              ::fit::function<void(zx_status_t)> error_handler);

  fuchsia::feedback::CrashReporter* CrashReporter();

  void ShutdownImminent();

 private:
  async_dispatcher_t* dispatcher_;

  std::shared_ptr<crash_reports::InfoContext> info_context_;
  crash_reports::LogTags tags_;
  crash_reports::CrashServer crash_server_;
  crash_reports::ReportStore report_store_;
  crash_reports::SnapshotCollector snapshot_collector_;
  crash_reports::CrashRegister crash_register_;
  crash_reports::CrashReporter crash_reporter_;

  crash_reports::MainServiceInfo info_;

  ::fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_connections_;
  ::fidl::BindingSet<fuchsia::feedback::CrashReportingProductRegister>
      crash_reporting_product_register_connections_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_CRASH_REPORTS_H_
