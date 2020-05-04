// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/utils.h"

#include <algorithm>

namespace feedback {

AnnotationKeys RestrictAllowlist(const AnnotationKeys& allowlist,
                                 const AnnotationKeys& restrict_to) {
  AnnotationKeys filtered;
  std::set_intersection(allowlist.begin(), allowlist.end(), restrict_to.begin(), restrict_to.end(),
                        std::inserter(filtered, filtered.begin()));
  return filtered;
}

std::vector<fuchsia::feedback::Annotation> ToFeedbackAnnotationVector(
    const Annotations& annotations) {
  std::vector<fuchsia::feedback::Annotation> vec;
  for (const auto& [key, value] : annotations) {
    if (value.HasValue()) {
      fuchsia::feedback::Annotation annotation;
      annotation.key = key;
      annotation.value = value.Value();
      vec.push_back(std::move(annotation));
    }
  }
  return vec;
}

}  // namespace feedback
