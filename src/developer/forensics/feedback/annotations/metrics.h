// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_METRICS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_METRICS_H_

#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics::feedback {

class AnnotationMetrics {
 public:
  explicit AnnotationMetrics(cobalt::Logger* cobalt);

  // Sends metrics related to |annotations| to Cobalt.
  void LogMetrics(const Annotations& annotations);

 private:
  cobalt::Logger* cobalt_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_METRICS_H_
