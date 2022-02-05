// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_

#include <lib/fpromise/promise.h>

#include <set>

#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

// Collects unsafe-to-cache annotations synchronously.
//
// Note: synchronous calls must be low-cost and return quickly, e.g. not IPC.
class DynamicSyncAnnotationProvider {
 public:
  // Returns the Annotations from this provider.
  virtual Annotations Get() = 0;
};

// Collects annotations not set by the platform.
class NonPlatformAnnotationProvider : public DynamicSyncAnnotationProvider {
 public:
  // Returns true if non-platform annotations are missing.
  virtual bool IsMissingAnnotations() const = 0;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_
