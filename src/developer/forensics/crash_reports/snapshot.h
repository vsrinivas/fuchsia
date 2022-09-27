// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <map>
#include <memory>
#include <string>

#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/utils/sized_data.h"

namespace forensics {
namespace crash_reports {

using SnapshotUuid = std::string;

// Allows for the data from a single FIDL fuchsia.feedback.Snapshot to be shared amongst many
// clients and managed by the SnapshotStore. The SnapshotStore may drop the underlying data at
// any point, however if a reference is held (gotten from LockArchive) the data will not be deleted
// until the last reference is deleted.
class ManagedSnapshot {
 public:
  struct Archive {
    Archive(const fuchsia::feedback::Attachment& archive);
    std::string key;
    SizedData value;
  };

  explicit ManagedSnapshot(std::weak_ptr<const Archive> archive) : archive_(std::move(archive)) {}

  std::shared_ptr<const Archive> LockArchive() const { return archive_.lock(); }

 private:
  std::weak_ptr<const Archive> archive_;
};

// Replacement for a ManagedSnapshot when the Snapshot manager drops a snapshot.
//
// |annotations_| stores information the SnapshotStore can collect immiediately when it's
// requested to get a snapshot, which may be dynamic and change with time. These data are things
// like channel and uptime.
//
// |presence_annotations_| store information from the SnapshotStore on the circumstances that
// caused the underlying data to be missing.
class MissingSnapshot {
 public:
  explicit MissingSnapshot(feedback::Annotations annotations,
                           feedback::Annotations presence_annotations)
      : annotations_(std::move(annotations)),
        presence_annotations_(std::move(presence_annotations)) {}

  const feedback::Annotations& Annotations() const { return annotations_; }

  // Information from the snapshot manager on why the snapshot is missing.
  const feedback::Annotations& PresenceAnnotations() const { return presence_annotations_; }

 private:
  feedback::Annotations annotations_;
  feedback::Annotations presence_annotations_;
};

using Snapshot = std::variant<ManagedSnapshot, MissingSnapshot>;

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_H_
