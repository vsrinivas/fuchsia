// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BUILD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BUILD_INFO_PROVIDER_H_

#include <lib/fit/promise.h>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"

namespace feedback {

// Collect the annotations related to the device's build info.
class BuildInfoProvider : public AnnotationProvider {
 public:
  explicit BuildInfoProvider(const AnnotationKeys& annotations_to_get);

  static AnnotationKeys GetSupportedAnnotations();

  fit::promise<Annotations> GetAnnotations() override;

 private:
  const AnnotationKeys annotations_to_get_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BUILD_INFO_PROVIDER_H_
