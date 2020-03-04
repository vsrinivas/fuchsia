// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_

#include <lib/fit/promise.h>

#include <optional>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"

namespace feedback {

class SingleSyncAnnotationProvider : public AnnotationProvider {
 public:
  SingleSyncAnnotationProvider(const AnnotationKey& key);
  virtual ~SingleSyncAnnotationProvider() = default;

  fit::promise<Annotations> GetAnnotations() override;

  virtual std::optional<AnnotationValue> GetAnnotation() = 0;

 private:
  const AnnotationKey key_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_SINGLE_SYNC_ANNOTATION_PROVIDER_H_
