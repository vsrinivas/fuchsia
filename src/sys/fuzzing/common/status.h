// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_STATUS_H_
#define SRC_SYS_FUZZING_COMMON_STATUS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/process.h>
#include <zircon/types.h>

namespace fuzzing {

using ::fuchsia::fuzzer::ProcessStats;
using ::fuchsia::fuzzer::Status;

// Utility method for copying status objects.
Status CopyStatus(const Status& status);

// Collect process-related statistics for a Zircon process. This function is kept standalone and
// separate from, e.g., the engine's |ProcessProxy| class or the target's |Process| class in order
// to be available to multiple usages when implementing FIDL methods within the controller.
zx_status_t GetStatsForProcess(const zx::process& process, ProcessStats* out);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_STATUS_H_
