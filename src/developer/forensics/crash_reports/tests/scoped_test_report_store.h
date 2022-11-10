// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_SCOPED_TEST_REPORT_STORE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_SCOPED_TEST_REPORT_STORE_H_

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/report_store.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {

// Handles boilerplate code for setting up parameters needed by ReportStore.
class ScopedTestReportStore {
 public:
  ScopedTestReportStore(
      feedback::AnnotationManager* annotation_manager, std::shared_ptr<InfoContext> info_context,
      StorageSize max_reports_tmp_size = crash_reports::kReportStoreMaxTmpSize,
      StorageSize max_reports_cache_size = crash_reports::kReportStoreMaxCacheSize,
      StorageSize max_snapshots_tmp_size = crash_reports::kSnapshotStoreMaxTmpSize,
      StorageSize max_snapshots_cache_size = crash_reports::kSnapshotStoreMaxCacheSize,
      StorageSize max_archives_size = StorageSize::Megabytes(1));
  ReportStore& GetReportStore();
  const std::string& GetTmpReportsPath() const;
  const std::string& GetCacheReportsPath() const;

 private:
  LogTags tags_;
  files::ScopedTempDir temp_dir_;
  const std::string tmp_reports_path_;
  const std::string cache_reports_path_;
  const std::string tmp_snapshots_path_;
  const std::string cache_snapshots_path_;

  std::unique_ptr<ReportStore> report_store_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_TESTS_SCOPED_TEST_REPORT_STORE_H_
