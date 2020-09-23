// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_

#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace crash_reports {

constexpr char kCrashServerKey[] = "crash_server";
constexpr char kCrashServerUploadPolicyKey[] = "upload_policy";
constexpr char kCrashServerUrlKey[] = "url";

// Snapshots can occupy up to 10 MB of memory.
constexpr StorageSize kSnapshotAnnotationsMaxSize = StorageSize::Megabytes(5);
constexpr StorageSize kSnapshotArchivesMaxSize = StorageSize::Megabytes(5);

// Other report data can occupy up to 5 MB of memory.
constexpr StorageSize kStoreMaxSize = StorageSize::Megabytes(5u);

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_
