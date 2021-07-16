// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_

#include <lib/zx/time.h>

#include "src/developer/forensics/utils/storage_size.h"

namespace forensics::feedback {

// Feedback gives the respective DirectoryMigrators 1 minutes to respond.
constexpr zx::duration kDirectoryMigratorResponeTimeout = zx::min(1);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Reboot reporting
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousGracefulRebootReasonFile[] = "/tmp/graceful_reboot_reason.txt";
constexpr char kCurrentGracefulRebootReasonFile[] = "/cache/graceful_reboot_reason.txt";
constexpr char kNotAFdrFile[] = "/data/not_a_fdr.txt";

// We file the crash report with a 90s delay to increase the likelihood that Inspect data (at
// all and specifically the data from memory_monitor) is included in the snapshot.zip generated
// by the Feedback service. The memory_monitor Inspect data is critical to debug OOM crash
// reports.
// TODO(fxbug.dev/46216, fxbug.dev/48485): remove delay.
constexpr zx::duration kOOMCrashReportingDelay = zx::sec(90);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Crash reporting
///////////////////////////////////////////////////////////////////////////////////////////////////

const char kBuildVersionPath[] = "/config/build-info/version";
const char kBuildBoardPath[] = "/config/build-info/board";
const char kBuildProductPath[] = "/config/build-info/product";
const char kBuildCommitDatePath[] = "/config/build-info/latest-commit-date";

const char kDefaultCrashReportsConfigPath[] = "/pkg/data/crash_reports/default_config.json";
const char kOverrideCrashReportsConfigPath[] = "/config/data/crash_reports/override_config.json";

constexpr char kCrashRegisterPath[] = "/tmp/crash_register.json";
constexpr char kCrashServerUrl[] = "https://clients2.google.com/cr/report";
constexpr char kGarbageCollectedSnapshotsPath[] = "/tmp/garbage_collected_snapshots.txt";

// Snapshots can occupy up to 10 MB of memory.
constexpr StorageSize kSnapshotAnnotationsMaxSize = StorageSize::Megabytes(5);
constexpr StorageSize kSnapshotArchivesMaxSize = StorageSize::Megabytes(5);

// If a crash report arrives within |kSnapshotSharedRequestWindow| of a call to
// SnapshotManager::GetSnapshotUuid that schedules a call to
// fuchsia.feedback.DataProvider/GetSnapshot, the returned snapshot will be used in the resulting
// report.
//
// If the value it too large, crash reports may take too long to generate, but if the value is too
// small, the benefits of combining calls to fuchsia.feedback.DataProvider/GetSnapshot may not be
// fully realized.
constexpr zx::duration kSnapshotSharedRequestWindow = zx::sec(5);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_
