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

constexpr char kUseRemoteDeviceIdProviderPath[] = "/config/data/feedback/remote_device_id_provider";

const char kDefaultBuildTypeConfigPath[] = "/pkg/data/build_type/default_config.json";
const char kOverrideBuildTypeConfigPath[] = "/config/data/build_type/override_config.json";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Reboot reporting
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousGracefulRebootReasonFile[] = "/tmp/graceful_reboot_reason.txt";
constexpr char kCurrentGracefulRebootReasonFile[] = "/data/graceful_reboot_reason.txt";

// TODO(fxbug.dev/85184): Stop attempting to read from the /cache file once it no longer exists.
constexpr char kLegacyGracefulRebootReasonFile[] = "/cache/graceful_reboot_reason.txt";

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
constexpr char kProductQuotasPath[] = "/cache/product_quotas.json";
constexpr char kCrashServerUrl[] = "https://clients2.google.com/cr/report";
constexpr char kGarbageCollectedSnapshotsPath[] = "/tmp/garbage_collected_snapshots.txt";

// Snapshots can occupy up to 10 MB of memory.
constexpr StorageSize kSnapshotArchivesMaxSize = StorageSize::Megabytes(10);

// If a crash report arrives within |kSnapshotSharedRequestWindow| of a call to
// SnapshotManager::GetSnapshotUuid that schedules a call to
// fuchsia.feedback.DataProvider/GetSnapshot, the returned snapshot will be used in the resulting
// report.
//
// If the value it too large, crash reports may take too long to generate, but if the value is too
// small, the benefits of combining calls to fuchsia.feedback.DataProvider/GetSnapshot may not be
// fully realized.
constexpr zx::duration kSnapshotSharedRequestWindow = zx::sec(5);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Feedback data
///////////////////////////////////////////////////////////////////////////////////////////////////
constexpr char kFeedbackDataConfigPath[] = "/pkg/data/feedback_data/config.json";

constexpr char kDeviceIdPath[] = "/data/device_id.txt";
constexpr char kCurrentLogsDir[] = "/cache/current_system_logs";
constexpr char kPreviousLogsFilePath[] = "/tmp/log.system.previous_boot.txt";
constexpr char kPreviousBootIdPath[] = "/tmp/boot_id.txt";
constexpr char kCurrentBootIdPath[] = "/data/boot_id.txt";
constexpr char kPreviousBuildVersionPath[] = "/tmp/build_version.txt";
constexpr char kCurrentBuildVersionPath[] = "/data/build_version.txt";
constexpr char kDataRegisterPath[] = "/tmp/data_register.json";

// Use this file to determine whether or not a previous instance of the component was instructed to
// terminated system log recording.
constexpr char kDoNotLaunchSystemLogRecorder[] = "/tmp/do_not_launch_system_log_recorder.txt";

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_
