// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_KEYS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_KEYS_H_

namespace forensics::feedback {

constexpr const char kBuildBoardKey[] = "build.board";
constexpr const char kBuildVersionKey[] = "build.version";
constexpr const char kBuildVersionPreviousBootKey[] = "build.version.previous-boot";
constexpr const char kBuildProductKey[] = "build.product";
constexpr const char kBuildLatestCommitDateKey[] = "build.latest-commit-date";
constexpr const char kBuildIsDebugKey[] = "build.is_debug";
constexpr const char kDeviceBoardNameKey[] = "device.board-name";
constexpr const char kSystemBootIdCurrentKey[] = "system.boot-id.current";
constexpr const char kSystemBootIdPreviousKey[] = "system.boot-id.previous";
constexpr const char kSystemLastRebootReasonKey[] = "system.last-reboot.reason";
constexpr const char kSystemLastRebootUptimeKey[] = "system.last-reboot.uptime";

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_KEYS_H_
