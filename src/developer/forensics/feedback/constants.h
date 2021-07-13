// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_

#include <lib/zx/time.h>

namespace forensics::feedback {

///////////////////////////////////////////////////////////////////////////////////////////////////
// Reboot reporting
///////////////////////////////////////////////////////////////////////////////////////////////////

constexpr char kPreviousGracefulRebootReasonFile[] = "/tmp/graceful_reboot_reason.txt";
constexpr char kCurrentGracefulRebootReasonFile[] = "/cache/graceful_reboot_reason.txt";
constexpr char kNotAFdrFile[] = "/data/not_a_fdr.txt";

// Feedback gives the respective DirectoryMigrators 1 minutes to respond.
constexpr zx::duration kDirectoryMigratorResponeTimeout = zx::min(1);

// We file the crash report with a 90s delay to increase the likelihood that Inspect data (at
// all and specifically the data from memory_monitor) is included in the snapshot.zip generated
// by the Feedback service. The memory_monitor Inspect data is critical to debug OOM crash
// reports.
// TODO(fxbug.dev/46216, fxbug.dev/48485): remove delay.
constexpr zx::duration kOOMCrashReportingDelay = zx::sec(90);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_CONSTANTS_H_
