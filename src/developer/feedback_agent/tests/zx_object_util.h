// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_ZX_OBJECT_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_ZX_OBJECT_UTIL_H_

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace fuchsia {
namespace feedback {

// TODO(frousseau): these should move to //garnet/public/lib/fsl with unit
// tests.

std::vector<zx::job> GetChildJobs(zx_handle_t job);

std::vector<zx::process> GetChildProcesses(zx_handle_t job);

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_ZX_OBJECT_UTIL_H_
