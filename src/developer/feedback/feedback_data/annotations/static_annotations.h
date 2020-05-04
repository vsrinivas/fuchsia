// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_

#include "src/developer/feedback/feedback_data/annotations/types.h"
#include "src/developer/feedback/feedback_data/device_id_provider.h"

namespace feedback {

// Synchronously fetches the static annotations, i.e. the annotations that don't change during a
// boot cycle.
Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 DeviceIdProvider* device_id_provider);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
