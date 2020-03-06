// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_DEVICE_ID_PROVIDER_H_

#include <optional>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/single_sync_annotation_provider.h"

namespace feedback {

// Collect the device's feedback id.
class DeviceIdProvider : public SingleSyncAnnotationProvider {
 public:
  DeviceIdProvider();

  static AnnotationKeys GetSupportedAnnotations();

  std::optional<AnnotationValue> GetAnnotation() override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
