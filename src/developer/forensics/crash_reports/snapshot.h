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
//
// The SnapshotManager may add annotations as |presence_annotations_| that convey information about
// how its management has affected the archive.
class ManagedSnapshot {
 public:
  struct Archive {
    Archive(const fuchsia::feedback::Attachment& archive);
    std::string key;
    SizedData value;
  };

  ManagedSnapshot(std::weak_ptr<const AnnotationMap> annotations,
                  std::weak_ptr<const AnnotationMap> presence_annotations,
                  std::weak_ptr<const Archive> archive = std::weak_ptr<const Archive>())
      : annotations_(std::move(annotations)),
        presence_annotations_(std::move(presence_annotations)),
        archive_(std::move(archive)) {}

  std::shared_ptr<const AnnotationMap> LockAnnotations() const { return annotations_.lock(); }

  std::shared_ptr<const AnnotationMap> LockPresenceAnnotations() const {
    return presence_annotations_.lock();
  }

  std::shared_ptr<const Archive> LockArchive() const { return archive_.lock(); }

 private:
  std::weak_ptr<const AnnotationMap> annotations_;
  std::weak_ptr<const AnnotationMap> presence_annotations_;
  std::weak_ptr<const Archive> archive_;
};

// Replacement for a ManagedSnapshot when the Snapshot manager drops a snapshot.
//
// |annotations_| stores information the SnapshotManager can collect immiediately when it's
// requested to get a snapshot, which may be dynamic and change with time. These data are things
// like channel and uptime.
//
// |presence_annotations_| store information from the SnapshotManager on the circumstances that
// caused the underlying data to be missing.
class MissingSnapshot {
 public:
  explicit MissingSnapshot(AnnotationMap annotations, AnnotationMap presence_annotations)
      : annotations_(std::move(annotations)),
        presence_annotations_(std::move(presence_annotations)) {}

  const AnnotationMap& Annotations() const { return annotations_; }

  // Information from the snapshot manager on why the snapshot is missing.
  const AnnotationMap& PresenceAnnotations() const { return presence_annotations_; }

 private:
  AnnotationMap annotations_;
  AnnotationMap presence_annotations_;
};

using Snapshot = std::variant<ManagedSnapshot, MissingSnapshot>;

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_
