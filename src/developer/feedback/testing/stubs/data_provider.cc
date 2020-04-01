// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/data_provider.h"

#include <lib/fit/result.h>
#include <zircon/errors.h>

#include <map>
#include <string>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace stubs {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::Data;

std::vector<Annotation> BuildAnnotations(const std::map<std::string, std::string>& annotations) {
  std::vector<Annotation> ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.push_back({key, value});
  }
  return ret_annotations;
}

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FX_CHECK(fsl::VmoFromString("unused", &attachment.value));
  return attachment;
}

}  // namespace

void DataProvider::GetData(GetDataCallback callback) {
  Data data;
  data.set_annotations(BuildAnnotations(annotations_));
  data.set_attachment_bundle(BuildAttachment(attachment_bundle_key_));
  callback(fit::ok(std::move(data)));
}

void DataProviderReturnsNoAnnotation::GetData(GetDataCallback callback) {
  Data data;
  data.set_attachment_bundle(BuildAttachment(attachment_bundle_key_));
  callback(fit::ok(std::move(data)));
}

void DataProviderReturnsNoAttachment::GetData(GetDataCallback callback) {
  Data data;
  data.set_annotations(BuildAnnotations(annotations_));
  callback(fit::ok(std::move(data)));
}

void DataProviderReturnsNoData::GetData(GetDataCallback callback) {
  callback(fit::error(ZX_ERR_INTERNAL));
}

void DataProviderNeverReturning::GetData(GetDataCallback callback) {}

void DataProviderBundleAttachment::GetData(GetDataCallback callback) {
  callback(fit::ok(
      std::move(fuchsia::feedback::Data().set_attachment_bundle(std::move(attachment_bundle_)))));
}

}  // namespace stubs
}  // namespace feedback
