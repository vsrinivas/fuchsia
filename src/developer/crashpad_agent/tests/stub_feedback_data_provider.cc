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

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString("unused", &attachment.value));
  return attachment;
}

}  // namespace

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  DataProvider_GetData_Result result;

  if (!next_annotation_keys_ && !next_attachment_keys_) {
    result.set_err(ZX_ERR_INTERNAL);
  } else {
    DataProvider_GetData_Response response;

    if (next_annotation_keys_) {
      std::vector<Annotation> annotations;
      for (const auto& key : *next_annotation_keys_.get()) {
        annotations.push_back(BuildAnnotation(key));
      }
      response.data.set_annotations(annotations);
      reset_annotation_keys();
    }

    if (next_attachment_keys_) {
      std::vector<Attachment> attachments;
      for (const auto& key : *next_attachment_keys_.get()) {
        attachments.push_back(BuildAttachment(key));
      }
      response.data.set_attachments(std::move(attachments));
      reset_attachment_keys();
    }

    result.set_response(std::move(response));
  }

  callback(std::move(result));
}

}  // namespace crash
}  // namespace fuchsia
