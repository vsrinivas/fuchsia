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

}  // namespace feedback
