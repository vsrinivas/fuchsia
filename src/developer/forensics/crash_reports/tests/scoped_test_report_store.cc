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
                                             StorageSize max_archives_size)
    : tmp_path_(files::JoinPath(temp_dir_.path(), kReportStoreTmpPath)),
      cache_path_(files::JoinPath(temp_dir_.path(), kReportStoreCachePath)) {
  files::CreateDirectory(tmp_path_);
  files::CreateDirectory(cache_path_);

  report_store_ = std::make_unique<ReportStore>(
      &tags_, std::move(info_context), annotation_manager,
      crash_reports::ReportStore::Root{tmp_path_, crash_reports::kReportStoreMaxTmpSize},
      crash_reports::ReportStore::Root{cache_path_, crash_reports::kReportStoreMaxCacheSize},
      kGarbageCollectedSnapshotsPath, max_archives_size);
}

ReportStore& ScopedTestReportStore::GetReportStore() { return *report_store_; }

const std::string& ScopedTestReportStore::GetTmpPath() const { return tmp_path_; }

const std::string& ScopedTestReportStore::GetCachePath() const { return cache_path_; }

}  // namespace forensics::crash_reports
