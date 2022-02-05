// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_

#include <memory>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Get the uptime of the device and the current UTC time.
class TimeProvider : public DynamicSyncAnnotationProvider {
 public:
  TimeProvider(std::unique_ptr<timekeeper::Clock> clock);

  Annotations Get() override;

 private:
  std::unique_ptr<timekeeper::Clock> clock_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIME_PROVIDER_H_
