// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/tests/stub_feedback_data_provider.h"

#include <lib/fsl/vmo/strings.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace crash {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::DataProvider_GetData_Response;
using fuchsia::feedback::DataProvider_GetData_Result;

Annotation BuildAnnotation(const std::string& key) {
  Annotation annotation;
  annotation.key = key;
  annotation.value = "unused";
  return annotation;
}

std::vector<Annotation> BuildAnnotations(
    const std::vector<std::string>& annotation_keys) {
  std::vector<Annotation> annotations;
  for (const auto& key : annotation_keys) {
    annotations.push_back(BuildAnnotation(key));
  }
  return annotations;
}

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString("unused", &attachment.value));
  return attachment;
}

std::vector<Attachment> BuildAttachments(
    const std::vector<std::string>& attachment_keys) {
  std::vector<Attachment> attachments;
  for (const auto& key : attachment_keys) {
    attachments.push_back(BuildAttachment(key));
  }
  return attachments;
}

}  // namespace

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  DataProvider_GetData_Result result;
  DataProvider_GetData_Response response;
  response.data.set_annotations(BuildAnnotations(annotation_keys_));
  response.data.set_attachments(BuildAttachments(attachment_keys_));
  result.set_response(std::move(response));
  callback(std::move(result));
}

void StubFeedbackDataProviderReturnsNoAnnotation::GetData(
    GetDataCallback callback) {
  DataProvider_GetData_Result result;
  DataProvider_GetData_Response response;
  response.data.set_attachments(BuildAttachments(attachment_keys_));
  result.set_response(std::move(response));
  callback(std::move(result));
}

void StubFeedbackDataProviderReturnsNoAttachment::GetData(
    GetDataCallback callback) {
  DataProvider_GetData_Result result;
  DataProvider_GetData_Response response;
  response.data.set_annotations(BuildAnnotations(annotation_keys_));
  result.set_response(std::move(response));
  callback(std::move(result));
}

void StubFeedbackDataProviderReturnsNoData::GetData(GetDataCallback callback) {
  DataProvider_GetData_Result result;
  result.set_err(ZX_ERR_INTERNAL);
  callback(std::move(result));
}

}  // namespace crash
}  // namespace fuchsia
