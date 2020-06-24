// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/utils.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {

std::vector<fuchsia::feedback::Attachment> ToFeedbackAttachmentVector(
    const Attachments& attachments) {
  std::vector<fuchsia::feedback::Attachment> vec;
  for (const auto& [key, value] : attachments) {
    if (!value.HasValue()) {
      continue;
    }

    fsl::SizedVmo vmo;
    if (!fsl::VmoFromString(value.Value(), &vmo)) {
      FX_LOGS(ERROR) << fxl::StringPrintf("Failed to convert attachment %s to VMO", key.c_str());
      continue;
    }

    fuchsia::feedback::Attachment attachment;
    attachment.key = key;
    attachment.value = std::move(vmo).ToTransport();
    vec.push_back(std::move(attachment));
  }
  return vec;
}

void AddToAttachments(const std::string& key, const std::string& value,
                      std::vector<fuchsia::feedback::Attachment>* attachments) {
  fuchsia::feedback::Attachment extra_attachment;
  extra_attachment.key = key;
  if (!fsl::VmoFromString(value, &extra_attachment.value)) {
    FX_LOGS(WARNING) << "Failed to convert value to VMO for " << key;
    return;
  }
  attachments->push_back(std::move(extra_attachment));
}

bool BundleAttachments(const std::vector<fuchsia::feedback::Attachment>& attachments,
                       fuchsia::feedback::Attachment* bundle) {
  if (!Archive(attachments, &(bundle->value))) {
    FX_LOGS(ERROR) << "Failed to archive attachments into one bundle";
    return false;
  }
  bundle->key = kBugreportFilename;
  return true;
}

}  // namespace feedback_data
}  // namespace forensics
