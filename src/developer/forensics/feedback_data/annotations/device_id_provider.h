// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/fidl/device_id_provider_ptr.h"

namespace forensics {
namespace feedback_data {

// Get the Feedback Id as an annotation.
class DeviceIdProviderClient : public AnnotationProvider {
 public:
  // fuchsia.feedback.DeviceIdProvider is expected to be in |services|.
  DeviceIdProviderClient(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<Annotations> GetAnnotations(zx::duration timeout,
                                             const AnnotationKeys& allowlist) override;

 private:
  fidl::DeviceIdProviderPtr device_id_provider_ptr_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_DEVICE_ID_PROVIDER_H_
