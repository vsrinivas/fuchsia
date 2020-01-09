// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/promise.h>

#include <optional>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"

namespace feedback {

class SingleSyncAnnotationProvider : public AnnotationProvider {
 public:
  SingleSyncAnnotationProvider(const std::string& key);
  virtual ~SingleSyncAnnotationProvider() = default;

  virtual std::optional<std::string> GetAnnotation() = 0;
  fit::promise<std::vector<fuchsia::feedback::Annotation>> GetAnnotations() override;

 private:
  const std::string key_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_
