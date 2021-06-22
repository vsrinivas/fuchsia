// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_

#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {

// Synchronously fetches the static annotations, i.e. the annotations that don't change during a
// boot cycle.
Annotations GetStaticAnnotations(const AnnotationKeys& allowlist,
                                 const ErrorOr<std::string>& current_boot_id,
                                 const ErrorOr<std::string>& previous_boot_id,
                                 const ErrorOr<std::string>& current_build_version,
                                 const ErrorOr<std::string>& previous_build_version);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_STATIC_ANNOTATIONS_H_
