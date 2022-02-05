// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_

#include <fuchsia/feedback/cpp/fidl.h>

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
// Annotations
///////////////////////////////////////////////////////////////////////////////////////////////////

const uint32_t kMaxNumPlatformAnnotations = 30u;
const uint32_t kMaxNumDebugAnnotations = 2u;
static_assert(kMaxNumPlatformAnnotations + feedback::kMaxNumPlatformAnnotations +
                      feedback::kMaxNumNonPlatformAnnotations + kMaxNumDebugAnnotations ==
                  fuchsia::feedback::MAX_NUM_ANNOTATIONS_PROVIDED,
              "The max number of provided annotations has to be split between a max number of "
              "platform annotations, a max number of non-platform annotations, and a max number of "
              "debug annotations");

// Platform annotation keys.
constexpr const char* kAnnotationBuildBoard = feedback::kBuildBoardKey;
constexpr const char* kAnnotationBuildIsDebug = feedback::kBuildIsDebugKey;
constexpr const char* kAnnotationBuildLatestCommitDate = feedback::kBuildLatestCommitDateKey;
constexpr const char* kAnnotationBuildProduct = feedback::kBuildProductKey;
constexpr const char* kAnnotationBuildVersion = feedback::kBuildVersionKey;
constexpr const char* kAnnotationBuildVersionPreviousBoot = feedback::kBuildVersionPreviousBootKey;
constexpr const char* kAnnotationDeviceBoardName = feedback::kDeviceBoardNameKey;
constexpr const char* kAnnotationDeviceFeedbackId = feedback::kDeviceFeedbackIdKey;
constexpr const char* kAnnotationHardwareBoardName = feedback::kHardwareBoardNameKey;
constexpr const char* kAnnotationHardwareBoardRevision = feedback::kHardwareBoardRevisionKey;
constexpr const char* kAnnotationHardwareProductLanguage = feedback::kHardwareProductLanguageKey;
constexpr const char* kAnnotationHardwareProductLocaleList =
    feedback::kHardwareProductLocaleListKey;
constexpr const char* kAnnotationHardwareProductManufacturer =
    feedback::kHardwareProductManufacturerKey;
constexpr const char* kAnnotationHardwareProductModel = feedback::kHardwareProductModelKey;
constexpr const char* kAnnotationHardwareProductName = feedback::kHardwareProductNameKey;
constexpr const char* kAnnotationHardwareProductRegulatoryDomain =
    feedback::kHardwareProductRegulatoryDomainKey;
constexpr const char* kAnnotationHardwareProductSKU = feedback::kHardwareProductSKUKey;
constexpr const char* kAnnotationSystemBootIdCurrent = feedback::kSystemBootIdCurrentKey;
constexpr const char* kAnnotationSystemBootIdPrevious = feedback::kSystemBootIdPreviousKey;
constexpr const char* kAnnotationSystemLastRebootReason = feedback::kSystemLastRebootReasonKey;
constexpr const char* kAnnotationSystemLastRebootUptime = feedback::kSystemLastRebootUptimeKey;
constexpr const char* kAnnotationSystemTimezonePrimary = feedback::kSystemTimezonePrimaryKey;
constexpr const char* kAnnotationSystemUpdateChannelCurrent =
    feedback::kSystemUpdateChannelCurrentKey;
constexpr const char* kAnnotationSystemUpdateChannelTarget =
    feedback::kSystemUpdateChannelTargetKey;

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
// Logs from previous boot cycle.
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousLogsFilePath[] = "/tmp/log.system.previous_boot.txt";

// We use the 8 files below to store up to 512 kb of logs. So, assuming all components have logged
// at least 512 kb of data, we can expect between 448 kb and 512 kb of logs to be persisted due to
// the log rotation.
constexpr StorageSize kPersistentLogsMaxSize = StorageSize::Kilobytes(512);
constexpr char kCurrentLogsDir[] = "/cache/current_system_logs";
constexpr size_t kMaxNumLogFiles = 8u;

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

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONSTANTS_H_
