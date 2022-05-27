// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/static_attachments.h"

#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <string>

#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/files/file.h"

namespace forensics::feedback {
namespace {

AttachmentValue FromFile(const std::string& filepath) {
  if (std::string content; files::ReadFileToString(filepath, &content)) {
    return content.empty() ? AttachmentValue(Error::kMissingValue)
                           : AttachmentValue(std::move(content));
  }

  FX_LOGS(WARNING) << "Failed to read: " << filepath;
  return AttachmentValue(Error::kFileReadFailure);
}

}  // namespace

Attachments GetStaticAttachments() {
  const std::map<std::string, AttachmentValue> kAttachments({
      {feedback_data::kAttachmentBuildSnapshot, FromFile("/config/build-info/snapshot")},
      {feedback_data::kAttachmentLogSystemPrevious, FromFile(feedback_data::kPreviousLogsFilePath)},
  });

  Attachments attachments;
  for (const auto& [k, v] : kAttachments) {
    attachments.insert({k, v});
    if (!attachments.at(k).HasValue()) {
      FX_LOGS(WARNING) << "Failed to build attachment " << k;
    }
  }

  return attachments;
}

}  // namespace forensics::feedback
