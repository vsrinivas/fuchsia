// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_TIME_PROVIDER_H_

#include <lib/fit/promise.h>

#include <memory>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {

// Get the uptime of the device and the current UTC time.
class TimeProvider : public AnnotationProvider {
 public:
  TimeProvider(const AnnotationKeys& annotations_to_get, std::unique_ptr<timekeeper::Clock> clock);

  static AnnotationKeys GetSupportedAnnotations();

  fit::promise<Annotations> GetAnnotations() override;

 private:
  AnnotationKeys annotations_to_get_;
  std::unique_ptr<timekeeper::Clock> clock_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_TIME_PROVIDER_H_
