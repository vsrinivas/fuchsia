// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_FEEDBACK_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_FEEDBACK_ID_PROVIDER_H_

#include <optional>
#include <set>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

namespace feedback {

// Collect the device's feedback id.
class FeedbackIdProvider : public SingleSyncAnnotationProvider {
 public:
  FeedbackIdProvider();

  static std::set<std::string> GetSupportedAnnotations();
  std::optional<std::string> GetAnnotation() override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_FEEDBACK_ID_PROVIDER_H_
