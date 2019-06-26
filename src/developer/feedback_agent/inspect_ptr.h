// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_INSPECT_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_INSPECT_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <zircon/time.h>

namespace fuchsia {
namespace feedback {

// Collects the Inspect data.
//
// Requires "shell" in the features of the calling component's sandbox to access
// the hub.
fit::promise<fuchsia::mem::Buffer> CollectInspectData(zx::duration timeout);

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_INSPECT_H_
