// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/previous_boot_log.h"

#include <lib/async/cpp/task.h>
#include <lib/fpromise/promise.h>

#include <string>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {

PreviousBootLog::PreviousBootLog(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                                 const zx::duration delete_previous_boot_log_at, std::string path)
    : dispatcher_(dispatcher), clock_(clock), path_(std::move(path)) {
  auto self = weak_factory_.GetWeakPtr();
  async::PostDelayedTask(
      dispatcher_,
      [self] {
        if (self) {
          FX_LOGS(INFO) << "Deleting previous boot logs after 24 hours of device uptime";
          files::DeletePath(self->path_, /*recursive=*/true);
        }
      },
      // The previous boot logs are deleted after |delete_previous_boot_log_at| of device uptime,
      // not component uptime.
      delete_previous_boot_log_at - zx::nsec(clock_->Now().get()));
}

::fpromise::promise<AttachmentValue> PreviousBootLog::Get(const uint64_t ticket) {
  AttachmentValue previous_boot_log(Error::kNotSet);
  if (std::string content; files::ReadFileToString(path_, &content)) {
    previous_boot_log = content.empty() ? AttachmentValue(Error::kMissingValue)
                                        : AttachmentValue(std::move(content));
  } else {
    FX_LOGS(WARNING) << "Failed to read: " << path_;
    previous_boot_log = AttachmentValue(Error::kFileReadFailure);
  }

  // The previous boot log is moved because it can be megabytes in size.
  return fpromise::make_ok_promise(std::move(previous_boot_log));
}

void PreviousBootLog::ForceCompletion(const uint64_t ticket, const Error error) {}

}  // namespace forensics::feedback
