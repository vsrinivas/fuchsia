// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_

#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/debuglog.h>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback_data {

// Retrieves the kernel log.
//
// fuchsia.boot.ReadOnlyLog is expected to be in |services|.
class KernelLog {
 public:
  KernelLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            std::unique_ptr<backoff::Backoff> backoff, RedactorBase* redactor);

  ::fpromise::promise<AttachmentValue> Get(zx::duration timeout);

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  RedactorBase* redactor_;

  fuchsia::boot::ReadOnlyLogPtr read_only_log_;

  // Calls to Get that haven't yet completed.
  std::vector<::fit::callback<void(std::variant<zx::debuglog, Error>)>> waiting_;

  fxl::WeakPtrFactory<KernelLog> ptr_factory_{this};
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_KERNEL_LOG_PTR_H_
