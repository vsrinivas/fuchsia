// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_

#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

// Responsible for the storage of annotations.
class AnnotationManager {
 public:
  AnnotationManager(Annotations static_annotations = {});

  // Inserts static, synchronous annotations.
  //
  // Note: annotation keys must be unique a check-fail will occur if |annotations| and
  // |static_annotations_| intersect.
  void InsertStatic(const Annotations& annotations);

  // Returns the annotations that are immediately available.
  const Annotations& ImmediatelyAvailable() const;

 private:
  Annotations static_annotations_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_
