// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/util.h"

#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/archive.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;

}  // namespace

void AddAnnotationsAsExtraAttachment(const std::vector<Annotation>& annotations,
                                     std::vector<Attachment>* attachments) {
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

  Attachment extra_attachment;
  extra_attachment.key = kAttachmentAnnotations;
  if (!fsl::VmoFromString(json_str, &extra_attachment.value)) {
    FX_LOGS(WARNING) << "Failed to write annotations as an extra attachment";
    return;
  }
  attachments->push_back(std::move(extra_attachment));
}

bool BundleAttachments(const std::vector<Attachment>& attachments, Attachment* bundle) {
  if (!Archive(attachments, &(bundle->value))) {
    FX_LOGS(ERROR) << "Failed to archive attachments into one bundle";
    return false;
  }
  bundle->key = kAttachmentBundle;
  return true;
}

}  // namespace feedback
