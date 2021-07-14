// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIME_PROVIDER_H_

#include <lib/fpromise/promise.h>

#include <memory>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Get the uptime of the device and the current UTC time.
class TimeProvider : public AnnotationProvider {
 public:
  TimeProvider(std::unique_ptr<timekeeper::Clock> clock);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  std::unique_ptr<timekeeper::Clock> clock_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIME_PROVIDER_H_
