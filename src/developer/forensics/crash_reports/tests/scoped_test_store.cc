// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/tests/scoped_test_store.h"

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {

ScopedTestStore::ScopedTestStore(feedback::AnnotationManager* annotation_manager,
                                 std::shared_ptr<InfoContext> info_context,
                                 StorageSize max_annotations_size, StorageSize max_archives_size)
    : tmp_path_(files::JoinPath(temp_dir_.path(), kStoreTmpPath)),
      cache_path_(files::JoinPath(temp_dir_.path(), kStoreCachePath)) {
  files::CreateDirectory(tmp_path_);
  files::CreateDirectory(cache_path_);

  store_ = std::make_unique<Store>(
      &tags_, std::move(info_context), annotation_manager,
      crash_reports::Store::Root{tmp_path_, crash_reports::kStoreMaxTmpSize},
      crash_reports::Store::Root{cache_path_, crash_reports::kStoreMaxCacheSize},
      kGarbageCollectedSnapshotsPath, max_annotations_size, max_archives_size);
}

Store& ScopedTestStore::GetStore() { return *store_; }

const std::string& ScopedTestStore::GetTmpPath() const { return tmp_path_; }

const std::string& ScopedTestStore::GetCachePath() const { return cache_path_; }

}  // namespace forensics::crash_reports
