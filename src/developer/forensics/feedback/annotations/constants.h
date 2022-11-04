// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_CONSTANTS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <set>
#include <string>

#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback {

///////////////////////////////////////////////////////////////////////////////////////////////////
// KEYS
///////////////////////////////////////////////////////////////////////////////////////////////////
constexpr const char kBuildBoardKey[] = "build.board";
constexpr const char kBuildVersionKey[] = "build.version";
constexpr const char kBuildVersionPreviousBootKey[] = "build.version.previous-boot";
constexpr const char kBuildProductKey[] = "build.product";
constexpr const char kBuildLatestCommitDateKey[] = "build.latest-commit-date";
constexpr const char kBuildIsDebugKey[] = "build.is_debug";
constexpr const char kDebugSnapshotErrorKey[] = "debug.snapshot.error";
constexpr const char kDebugSnapshotPresentKey[] = "debug.snapshot.present";
constexpr const char kDeviceBoardNameKey[] = "device.board-name";
constexpr const char kDeviceFeedbackIdKey[] = "device.feedback-id";
constexpr const char kDeviceNumCPUsKey[] = "device.num-cpus";
constexpr const char kDeviceUptimeKey[] = "device.uptime";
constexpr const char kDeviceUtcTimeKey[] = "device.utc-time";
constexpr const char kHardwareBoardNameKey[] = "hardware.board.name";
constexpr const char kHardwareBoardRevisionKey[] = "hardware.board.revision";
constexpr const char kHardwareProductLanguageKey[] = "hardware.product.language";
constexpr const char kHardwareProductLocaleListKey[] = "hardware.product.locale-list";
constexpr const char kHardwareProductManufacturerKey[] = "hardware.product.manufacturer";
constexpr const char kHardwareProductModelKey[] = "hardware.product.model";
constexpr const char kHardwareProductNameKey[] = "hardware.product.name";
constexpr const char kHardwareProductRegulatoryDomainKey[] = "hardware.product.regulatory-domain";
constexpr const char kHardwareProductSKUKey[] = "hardware.product.sku";
constexpr const char kOSNameKey[] = "osName";
constexpr const char kOSVersionKey[] = "osVersion";
constexpr const char kOSChannelKey[] = "osChannel";
constexpr const char kSystemBootIdCurrentKey[] = "system.boot-id.current";
constexpr const char kSystemBootIdPreviousKey[] = "system.boot-id.previous";
constexpr const char kSystemLastRebootReasonKey[] = "system.last-reboot.reason";
constexpr const char kSystemLastRebootUptimeKey[] = "system.last-reboot.uptime";
constexpr const char kSystemTimezonePrimaryKey[] = "system.timezone.primary";
constexpr const char kSystemUpdateChannelCurrentKey[] = "system.update-channel.current";
constexpr const char kSystemUpdateChannelTargetKey[] = "system.update-channel.target";
constexpr const char kSystemUserActivityCurrentStateKey[] = "system.user-activity.current.state";
constexpr const char kSystemUserActivityCurrentDurationKey[] =
    "system.user-activity.current.duration";

///////////////////////////////////////////////////////////////////////////////////////////////////
// RESTRICTIONS
///////////////////////////////////////////////////////////////////////////////////////////////////

// 32 annotations may be collected by the platform.
constexpr uint32_t kMaxNumPlatformAnnotations = 32u;

// 30 non-platform annotations may be registered by non-platform components.
constexpr uint32_t kMaxNumNonPlatformAnnotations = 30u;

// 2 annotations are permitted to be from Feedback itself for debugging purposes.
constexpr uint32_t kMaxNumDebugAnnotations = 2u;

static_assert(kMaxNumPlatformAnnotations + kMaxNumNonPlatformAnnotations +
                      kMaxNumDebugAnnotations ==
                  fuchsia::feedback::MAX_NUM_ANNOTATIONS_PROVIDED,
              "Annotations must be allocated to the platform, non-platform components, and "
              "Feedback itself (for debugging)");

// Reserved namespaces for platform annotations. Components are not allowed to use these namespaces
// when supplying non-platform annotations.
const std::set<std::string> kReservedAnnotationNamespaces({
    "build",
    "device",
    "hardware",
    "hardware.board",
    "hardware.product",
    "misc",
    "system",
});

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_CONSTANTS_H_
