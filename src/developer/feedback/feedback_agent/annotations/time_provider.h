// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_UPTIME_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_UPTIME_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/promise.h>

#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {

// Get the uptime of the device and the current UTC time.
class TimeProvider : public AnnotationProvider {
 public:
  TimeProvider(const std::set<std::string>& annotations_to_get,
               std::unique_ptr<timekeeper::Clock> clock);
  static std::set<std::string> GetSupportedAnnotations();
  fit::promise<std::vector<fuchsia::feedback::Annotation>> GetAnnotations() override;

 private:
  std::set<std::string> annotations_to_get_;
  std::unique_ptr<timekeeper::Clock> clock_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_UPTIME_PROVIDER_H_
