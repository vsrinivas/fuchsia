// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_KERNEL_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_KERNEL_LOG_H_

#include <fuchsia/boot/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/debuglog.h>

#include "src/developer/forensics/feedback/attachments/provider.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback {

// Retrieves the kernel log.
//
// fuchsia.boot.ReadOnlyLog is expected to be in |services|.
class KernelLog : public AttachmentProvider {
 public:
  KernelLog(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
            std::unique_ptr<backoff::Backoff> backoff, RedactorBase* redactor);

  // Returns a promise to the kernel log and allows collection to be terminated early with
  // |ticket|.
  ::fpromise::promise<AttachmentValue> Get(uint64_t ticket) override;

  // Completes the kernel log collection promise associated with |ticket| early, if it hasn't
  // already completed.
  void ForceCompletion(uint64_t ticket, Error error) override;

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<backoff::Backoff> backoff_;
  RedactorBase* redactor_;

  fuchsia::boot::ReadOnlyLogPtr read_only_log_;

  // Calls to Get that haven't yet completed.
  std::vector<::fit::callback<void(std::variant<zx::debuglog, Error>)>> waiting_;

  std::map<uint64_t, ::fit::callback<void(std::variant<zx::debuglog, Error>)>> completers_;

  fxl::WeakPtrFactory<KernelLog> ptr_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_KERNEL_LOG_H_
