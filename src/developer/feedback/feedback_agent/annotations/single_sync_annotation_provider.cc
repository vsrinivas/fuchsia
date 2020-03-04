// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

#include <lib/fit/promise.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

SingleSyncAnnotationProvider::SingleSyncAnnotationProvider(const std::string& key) : key_(key) {}

fit::promise<Annotations> SingleSyncAnnotationProvider::GetAnnotations() {
  const auto annotation_value = GetAnnotation();
  if (!annotation_value.has_value()) {
    FX_LOGS(WARNING) << "Failed to build annotation " << key_;
    return fit::make_result_promise<Annotations>(fit::error());
  }

  return fit::make_ok_promise(Annotations({{key_, annotation_value.value()}}));
}

}  // namespace feedback
