// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_

#include <lib/zx/time.h>

#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace crash_reports {

constexpr char kCrashReporterKey[] = "crash_reporter";
constexpr char kDailyPerProductQuotaKey[] = "daily_per_product_quota";

constexpr char kCrashServerKey[] = "crash_server";
constexpr char kCrashServerUploadPolicyKey[] = "upload_policy";

constexpr char kHourlySnapshot[] = "hourly_snapshot";
constexpr char kHourlySnapshotProgramName[] = "system";
constexpr char kHourlySnapshotSignature[] = "fuchsia-hourly-snapshot";

constexpr char kCrashServerUrl[] = "https://clients2.google.com/cr/report";

constexpr const char* kGarbageCollectedSnapshotsPath = "/tmp/garbage_collected_snapshots.txt";

// Up to 512KiB of the non-snapshot portion of reports, like annotations and the minidump, are
// stored on disk under /cache/reports. This allows some report data to be uploaded in the event of
// a device shutdown.
//
// When a crash occurs, we check if its non-snapshot parts will fit in the remaining space
// alloted to /cache. If there is enough space available, the report is written to /cache, otherwise
// it is written to /tmp. Once in /cache those reports are not subject to garbage collection, unlike
// /tmp; they are only deleted once the report is no longer needed by the component.
constexpr const char* kReportStoreTmpPath = "/tmp/reports";
constexpr const char* kReportStoreCachePath = "/cache/reports";

// Other report data can occupy up to 5 MB of memory and disk.
constexpr StorageSize kReportStoreMaxSize = StorageSize::Megabytes(5u);

// Minidumps and annotations (the two most common non-snapshot files in crash reports) are usually
// in the order of 64 - 128KiB. This lets a device store 4-8 of them on disk.
constexpr StorageSize kReportStoreMaxCacheSize = StorageSize::Kilobytes(512);
constexpr StorageSize kReportStoreMaxTmpSize = kReportStoreMaxSize - kReportStoreMaxCacheSize;

// If a crash report arrives within |kSnapshotSharedRequestWindow| of a call to
// SnapshotManager::GetSnapshotUuid that schedules a call to
// fuchsia.feedback.DataProvider/GetSnapshot, the returned snapshot will be used in the resulting
// report.
//
// If the value it too large, crash reports may take too long to generate, but if the value is too
// small, the benefits of combining calls to fuchsia.feedback.DataProvider/GetSnapshot may not be
// fully realized.
constexpr zx::duration kSnapshotSharedRequestWindow = zx::sec(5);

// Snapshot uuids used when no snapshot.zip file could be included in the crash
// report.
//
// The underlying snapshot zip file was dropped due to space constraints.
constexpr const char* kGarbageCollectedSnapshotUuid = "garbage collected";

// A snapshot wasn't able to be persisted before a device shutdown.
constexpr const char* kNotPersistedSnapshotUuid = "not persisted";

// Snapshot collection terminated prematurely due to time constraints.
constexpr const char* kTimedOutSnapshotUuid = "timed out";

// Snapshot collection wasn't attempted because the system was in the process of shutting down.
constexpr const char* kShutdownSnapshotUuid = "shutdown";

// Uuid a client can use if it doesn't have one, e.g., it was previously stored in a file and the
// file is gone.
constexpr const char* kNoUuidSnapshotUuid = "no uuid";

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_CONSTANTS_H_
