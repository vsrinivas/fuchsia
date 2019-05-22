// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/bugreport/tests/stub_feedback_data_provider.h"

#include <lib/fsl/vmo/strings.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace bugreport {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::DataProvider_GetData_Response;
using fuchsia::feedback::DataProvider_GetData_Result;

Annotation BuildAnnotation(const std::string& key, const std::string& value) {
  Annotation annotation;
  annotation.key = key;
  annotation.value = value;
  return annotation;
}

std::vector<Annotation> BuildAnnotations(
    const std::map<std::string, std::string>& annotation_map) {
  std::vector<Annotation> annotations;
  for (const auto& [key, value] : annotation_map) {
    annotations.push_back(BuildAnnotation(key, value));
  }
  return annotations;
}

Attachment BuildAttachment(const std::string& key, const std::string& value) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString(value, &attachment.value));
  return attachment;
}

std::vector<Attachment> BuildAttachments(
    const std::map<std::string, std::string>& attachment_map) {
  std::vector<Attachment> attachments;
  for (const auto& [key, value] : attachment_map) {
    attachments.push_back(BuildAttachment(key, value));
  }
  return attachments;
}

}  // namespace

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  DataProvider_GetData_Result result;
  DataProvider_GetData_Response response;
  response.data.set_annotations(BuildAnnotations(annotations_));
  response.data.set_attachments(BuildAttachments(attachments_));
  result.set_response(std::move(response));
  callback(std::move(result));
}

}  // namespace bugreport
}  // namespace fuchsia
