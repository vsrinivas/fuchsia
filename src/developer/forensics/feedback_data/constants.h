// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace feedback_data {

///////////////////////////////////////////////////////////////////////////////////////////////////
// Attachments
///////////////////////////////////////////////////////////////////////////////////////////////////

// Platform attachments keys.
constexpr char kAttachmentAnnotations[] = "annotations.json";
constexpr char kAttachmentBuildSnapshot[] = "build.snapshot.xml";
constexpr char kAttachmentInspect[] = "inspect.json";
constexpr char kAttachmentLogKernel[] = "log.kernel.txt";
constexpr char kAttachmentLogSystem[] = "log.system.txt";
constexpr char kAttachmentLogSystemPrevious[] = "log.system.previous_boot.txt";
constexpr char kAttachmentMetadata[] = "metadata.json";

// Snapshot key.
constexpr char kSnapshotFilename[] = "snapshot.zip";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Device ID
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kDeviceIdPath[] = "/data/device_id.txt";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Boot ID
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousBootIdPath[] = "/tmp/boot_id.txt";
constexpr char kCurrentBootIdPath[] = "/data/boot_id.txt";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Build version
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousBuildVersionPath[] = "/tmp/build_version.txt";
constexpr char kCurrentBuildVersionPath[] = "/data/build_version.txt";

///////////////////////////////////////////////////////////////////////////////////////////////////
// UTC-monotonic difference
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kUtcMonotonicDifferenceFile[] = "current_utc_monotonic_difference.txt";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Logs from current boot.
///////////////////////////////////////////////////////////////////////////////////////////////////

// Buffer up to 4MiB of logs in memory.
constexpr StorageSize kCurrentLogBufferSize = StorageSize::Megabytes(4);

// Stream and buffer logs for 5 minutes after a snapshot collected.
//
// TODO(fxbug.dev/99223): Set the default value for userdebug to at least 1 hour. Logs will be
// streamed indefinitely because of hourly snapshots.
constexpr zx::duration kActiveLoggingPeriod = zx::min(5);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Logs from previous boot cycle.
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousLogsFilePath[] = "/tmp/log.system.previous_boot.txt";

constexpr char kCurrentLogsDir[] = "/cache/current_system_logs";

// At most 16KB of logs will be persisted each second.
constexpr StorageSize kMaxWriteSize = StorageSize::Kilobytes(16);

// Repeated messge format
constexpr char kRepeatedStrPrefix[] = "!!! MESSAGE REPEATED ";
constexpr char kRepeatedOnceFormatStr[] = "!!! MESSAGE REPEATED 1 MORE TIME !!!\n";
constexpr char kRepeatedFormatStr[] = "!!! MESSAGE REPEATED %lu MORE TIMES !!!\n";

// Message when the Stop signal is received.
constexpr char kStopMessageStr[] =
    "!!! SYSTEM SHUTDOWN SIGNAL RECEIVED FURTHER LOGS ARE NOT GUARANTEED !!!\n";

// One repeated message can occupy up to "kMaxRepeatedBuffers" buffers.
constexpr size_t kMaxRepeatedBuffers = 30;

// The current version of the snapshot. Update these values together!
struct SnapshotVersion {
  static constexpr cobalt::SnapshotVersion kCobalt = cobalt::SnapshotVersion::kV_01;
  static constexpr const char* kString = "1";
};

// Use this file to determine whether or not a previous instance of the component was instructed to
// terminated system log recording.
constexpr char kDoNotLaunchSystemLogRecorder[] = "/tmp/do_not_launch_system_log_recorder.txt";

// The name of the protocol to use to read Feedback data from the Archive.
constexpr char kArchiveAccessorName[] = "fuchsia.diagnostics.FeedbackArchiveAccessor";

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_
