// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_KERNEL_LOG_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_KERNEL_LOG_PTR_H_

#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/time.h>

#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Retrieves the kernel log. fuchsia.boot.ReadOnlyLog is expected to be in
// |services|.
fit::promise<fuchsia::mem::Buffer> CollectKernelLog(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    zx::duration timeout);

// Wraps around fuchsia::boot::ReadOnlyLogPtr to handle establishing the
// connection, losing the connection, waiting for the callback, enforcing a
// timeout, etc.
//
// GetLog() is expected to be called only once.
class BootLog {
 public:
  BootLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<fuchsia::mem::Buffer> GetLog(zx::duration timeout);

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  // Enforces the one-shot nature of GetLog().
  bool has_called_get_log_ = false;

  fuchsia::boot::ReadOnlyLogPtr log_ptr_;
  fit::bridge<fuchsia::mem::Buffer> done_;
  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if we are done another way.
  fxl::CancelableClosure done_after_timeout_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BootLog);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_KERNEL_LOG_PTR_H_
