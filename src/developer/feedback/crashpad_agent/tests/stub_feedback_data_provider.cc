// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_data_provider.h"

#include <lib/fit/result.h>
#include <lib/fsl/vmo/strings.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::Data;

Annotation BuildAnnotation(const std::string& key) {
  Annotation annotation;
  annotation.key = key;
  annotation.value = "unused";
  return annotation;
}

std::vector<Annotation> BuildAnnotations(const std::vector<std::string>& annotation_keys) {
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

}  // namespace

void StubFeedbackDataProvider::GetData(GetDataCallback callback) {
  Data data;
  data.set_annotations(BuildAnnotations(annotation_keys_));
  data.set_attachment_bundle(BuildAttachment(attachment_bundle_key_));
  callback(fit::ok(std::move(data)));
}

void StubFeedbackDataProviderReturnsNoAnnotation::GetData(GetDataCallback callback) {
  Data data;
  data.set_attachment_bundle(BuildAttachment(attachment_bundle_key_));
  callback(fit::ok(std::move(data)));
}

void StubFeedbackDataProviderReturnsNoAttachment::GetData(GetDataCallback callback) {
  Data data;
  data.set_annotations(BuildAnnotations(annotation_keys_));
  callback(fit::ok(std::move(data)));
}

void StubFeedbackDataProviderReturnsNoData::GetData(GetDataCallback callback) {
  callback(fit::error(ZX_ERR_INTERNAL));
}

void StubFeedbackDataProviderNeverReturning::GetData(GetDataCallback callback) {}

}  // namespace feedback
