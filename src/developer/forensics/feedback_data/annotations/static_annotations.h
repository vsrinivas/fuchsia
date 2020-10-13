// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_

#include "src/developer/forensics/feedback_data/annotations/types.h"

namespace forensics {
namespace feedback_data {

// Synchronously fetches the static annotations, i.e. the annotations that don't change during a
// boot cycle.
Annotations GetStaticAnnotations(const AnnotationKeys& allowlist);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
