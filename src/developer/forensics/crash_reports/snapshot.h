// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/forensics/crash_reports/annotation_map.h"
#include "src/developer/forensics/utils/sized_data.h"

namespace forensics {
namespace crash_reports {

// Allows for the data from a single FIDL fuchsia.feedback.Snapshot to be shared amongst many
// clients and managed by the SnapshotManager. The SnapshotManager may drop the annotations or
// archive at any point, however if a reference is held (gotten from LockAnnotations or
// LockArchive) the data will not be deleted until the last reference is deleted.
class Snapshot {
 public:
  struct Archive {
    Archive(const fuchsia::feedback::Attachment& archive);
    std::string key;
    SizedData value;
  };

  explicit Snapshot(std::weak_ptr<AnnotationMap> annotations)
      : annotations_(std::move(annotations)), archive_(std::weak_ptr<Archive>()) {}

  Snapshot(std::weak_ptr<AnnotationMap> annotations, std::weak_ptr<Archive> archive)
      : annotations_(std::move(annotations)), archive_(std::move(archive)) {}

  std::shared_ptr<AnnotationMap> LockAnnotations() const { return annotations_.lock(); }

  std::shared_ptr<Archive> LockArchive() const { return archive_.lock(); }

 private:
  std::weak_ptr<AnnotationMap> annotations_;
  std::weak_ptr<Archive> archive_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_
