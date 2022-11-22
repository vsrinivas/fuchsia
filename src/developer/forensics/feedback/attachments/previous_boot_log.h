// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PREVIOUS_BOOT_LOG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PREVIOUS_BOOT_LOG_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>

#include <memory>
#include <string>

#include "src/developer/forensics/feedback/attachments/provider.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Collects the previous boot log and also deletes previous boot log after
// |delete_previous_boot_log_at| of device uptime.
class PreviousBootLog : public AttachmentProvider {
 public:
  PreviousBootLog(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                  zx::duration delete_previous_boot_log_at, std::string path);

  // Returns a promise, that is immediately available, to the previous boot log.
  ::fpromise::promise<AttachmentValue> Get(uint64_t ticket) override;

  // No-op because collection happens synchronously
  void ForceCompletion(uint64_t ticket, Error error) override;

 private:
  async_dispatcher_t* dispatcher_;

  timekeeper::Clock* clock_;
  std::string path_;
  fxl::WeakPtrFactory<PreviousBootLog> weak_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_PREVIOUS_BOOT_LOG_H_
