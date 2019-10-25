// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_PTR_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <zircon/time.h>

#include <memory>
#include <mutex>
#include <vector>

#include "src/lib/inspect_deprecated/query/location.h"

namespace feedback {

// Collects the Inspect data.
//
// Requires "hub" in the features of the calling component's sandbox to access the hub.
fit::promise<fuchsia::mem::Buffer> CollectInspectData(async_dispatcher_t* dispatcher,
                                                      zx::duration timeout);

// Wrapper around the Inspect data collection to track the lifetime of the objects more easily.
class Inspect {
 public:
  Inspect(async_dispatcher_t* dispatcher);

  fit::promise<fuchsia::mem::Buffer> Collect(zx::duration timeout);

 private:
  async_dispatcher_t* dispatcher_;
  // Enforces the one-shot nature of Collect().
  bool has_called_collect_ = false;

  std::shared_ptr<fit::bridge<std::vector<inspect_deprecated::Location>>> discovery_done_;
  std::shared_ptr<std::mutex> discovery_done_lock_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_INSPECT_PTR_H_
