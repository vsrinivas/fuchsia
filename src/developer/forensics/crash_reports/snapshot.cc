// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics {
namespace crash_reports {

bool IsSpecialCaseSnapshot(const SnapshotUuid& uuid) {
  return uuid == kNoUuidSnapshotUuid || uuid == kGarbageCollectedSnapshotUuid ||
         uuid == kShutdownSnapshotUuid || uuid == kTimedOutSnapshotUuid ||
         uuid == kNotPersistedSnapshotUuid;
}

ManagedSnapshot::Archive::Archive(const fuchsia::feedback::Attachment& attachment)
    : key(attachment.key), value() {
  const auto& archive = attachment.value;
  if (!archive.vmo.is_valid()) {
    return;
  }

  value = SizedData(archive.size, 0u);
  if (const zx_status_t status =
          archive.vmo.read(value.data(), /*offset=*/0u, /*len=*/value.size());
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to read vmo";
    return;
  }
}

ManagedSnapshot::Archive::Archive(std::string archive_key, SizedData archive)
    : key(std::move(archive_key)), value(std::move(archive)) {}

}  // namespace crash_reports
}  // namespace forensics
