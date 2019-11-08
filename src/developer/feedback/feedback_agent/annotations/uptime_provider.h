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

#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

namespace feedback {

// Get the uptime of the device.
class UptimeProvider : public SingleSyncAnnotationProvider {
 public:
  UptimeProvider();
  static std::set<std::string> GetSupportedAnnotations();
  std::optional<std::string> GetAnnotation() override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_UPTIME_PROVIDER_H_
