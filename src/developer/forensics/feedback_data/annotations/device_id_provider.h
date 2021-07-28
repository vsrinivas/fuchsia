// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_

#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"

namespace forensics {
namespace feedback_data {

// Get the Feedback Id as an annotation.
class DeviceIdProviderClient : public AnnotationProvider {
 public:
  // fuchsia.feedback.DeviceIdProvider is expected to be in |services|.
  explicit DeviceIdProviderClient(feedback::DeviceIdProvider* device_id_provider);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  feedback::DeviceIdProvider* device_id_provider_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
