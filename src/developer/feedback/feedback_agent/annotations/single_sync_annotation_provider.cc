// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using fuchsia::feedback::Annotation;

SingleSyncAnnotationProvider::SingleSyncAnnotationProvider(const std::string& key) : key_(key) {}

fit::promise<std::vector<fuchsia::feedback::Annotation>>
SingleSyncAnnotationProvider::GetAnnotations() {
  const auto annotation_value = GetAnnotation();
  if (!annotation_value) {
    FX_LOGS(WARNING) << "Failed to build annotation " << key_;
    return {};
  }

  Annotation annotation;
  annotation.key = key_;
  annotation.value = std::move(annotation_value.value());

  std::vector<Annotation> annotations;
  annotations.push_back(std::move(annotation));

  return fit::make_ok_promise(std::move(annotations));
}

}  // namespace feedback
