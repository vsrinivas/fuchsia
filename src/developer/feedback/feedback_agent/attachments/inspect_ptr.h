// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <zircon/time.h>

#include <memory>
#include <mutex>

namespace feedback {

// Collects the Inspect data.
//
// Requires a dispatcher on which to post the delayed task for the timeout and an executor on which
// to run the Inspect data collection. They must run on different loops and threads as the executor
// could have hanging tasks blocked on synchronous I/O operations.
//
// Requires "hub" in the features of the calling component's sandbox to access the hub.
fit::promise<fuchsia::mem::Buffer> CollectInspectData(async_dispatcher_t* timeout_dispatcher,
                                                      zx::duration timeout,
                                                      async::Executor* collection_executor);

// Wrapper around the Inspect data collection to track the lifetime of the objects more easily.
class Inspect {
 public:
  Inspect(async_dispatcher_t* timeout_dispatcher, async::Executor* collection_executor);

  fit::promise<fuchsia::mem::Buffer> Collect(zx::duration timeout);

 private:
  async_dispatcher_t* timeout_dispatcher_;
  async::Executor* collection_executor_;
  // Enforces the one-shot nature of Collect().
  bool has_called_collect_ = false;

  std::shared_ptr<fit::bridge<fuchsia::mem::Buffer>> collection_done_;
  std::shared_ptr<std::mutex> collection_done_lock_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_INSPECT_PTR_H_
