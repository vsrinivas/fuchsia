// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/attachments/utils.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/archive.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace feedback {

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

void AddAnnotationsAsExtraAttachment(const std::vector<fuchsia::feedback::Annotation>& annotations,
                                     std::vector<fuchsia::feedback::Attachment>* attachments) {
  rapidjson::Document json;
  json.SetObject();
  for (const auto& annotation : annotations) {
    json.AddMember(rapidjson::StringRef(annotation.key), annotation.value, json.GetAllocator());
  }
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  if (!writer.IsComplete()) {
    FX_LOGS(WARNING) << "Failed to write annotations as a JSON";
    return;
  }
  std::string json_str(buffer.GetString(), buffer.GetSize());

  fuchsia::feedback::Attachment extra_attachment;
  extra_attachment.key = kAttachmentAnnotations;
  if (!fsl::VmoFromString(json_str, &extra_attachment.value)) {
    FX_LOGS(WARNING) << "Failed to write annotations as an extra attachment";
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
  bundle->key = kAttachmentBundle;
  return true;
}

}  // namespace feedback
