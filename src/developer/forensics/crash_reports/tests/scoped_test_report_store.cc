// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/tests/scoped_test_report_store.h"

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {

ScopedTestReportStore::ScopedTestReportStore(feedback::AnnotationManager* annotation_manager,
                                             std::shared_ptr<InfoContext> info_context,
                                             const StorageSize max_reports_tmp_size,
                                             const StorageSize max_reports_cache_size,
                                             const StorageSize max_snapshots_tmp_size,
                                             const StorageSize max_snapshots_cache_size,
                                             const StorageSize max_archives_size)
    : tmp_reports_path_(files::JoinPath(temp_dir_.path(), kReportStoreTmpPath)),
      cache_reports_path_(files::JoinPath(temp_dir_.path(), kReportStoreCachePath)),
      tmp_snapshots_path_(files::JoinPath(temp_dir_.path(), kSnapshotStoreTmpPath)),
      cache_snapshots_path_(files::JoinPath(temp_dir_.path(), kSnapshotStoreCachePath)) {
  files::CreateDirectory(tmp_reports_path_);
  files::CreateDirectory(cache_reports_path_);
  files::CreateDirectory(tmp_snapshots_path_);
  files::CreateDirectory(cache_snapshots_path_);

  report_store_ = std::make_unique<ReportStore>(
      &tags_, std::move(info_context), annotation_manager,
      /*temp_reports_root=*/
      crash_reports::ReportStore::Root{tmp_reports_path_, max_reports_tmp_size},
      /*persistent_reports_root=*/
      crash_reports::ReportStore::Root{cache_reports_path_, max_reports_cache_size},
      /*temp_snapshots_root=*/
      crash_reports::SnapshotPersistence::Root{tmp_snapshots_path_, max_snapshots_tmp_size},
      /*persistent_snapshots_root=*/
      crash_reports::SnapshotPersistence::Root{cache_snapshots_path_, max_snapshots_cache_size},
      files::JoinPath(temp_dir_.path(), kGarbageCollectedSnapshotsPath), max_archives_size);
}

ReportStore& ScopedTestReportStore::GetReportStore() { return *report_store_; }

const std::string& ScopedTestReportStore::GetTmpReportsPath() const { return tmp_reports_path_; }

const std::string& ScopedTestReportStore::GetCacheReportsPath() const {
  return cache_reports_path_;
}

}  // namespace forensics::crash_reports
