// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_

#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/debuglog.h>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {

// Retrieves the kernel log. fuchsia.boot.ReadOnlyLog is expected to be in
// |services|.
::fpromise::promise<AttachmentValue> CollectKernelLog(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout);

// Wraps around fuchsia::boot::ReadOnlyLogPtr to handle establishing the
// connection, losing the connection, waiting for the callback, enforcing a
// timeout, etc.
//
// GetLog() is expected to be called only once.
class BootLog {
 public:
  BootLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fpromise::promise<AttachmentValue> GetLog(fit::Timeout timeout);

 private:
  fidl::OneShotPtr<fuchsia::boot::ReadOnlyLog, std::string> log_ptr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BootLog);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_
