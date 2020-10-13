// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/device_id_provider.h"

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationDeviceFeedbackId,
};

}  // namespace

DeviceIdProviderClient::DeviceIdProviderClient(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services)
    : device_id_provider_ptr_(dispatcher, services) {}

::fit::promise<Annotations> DeviceIdProviderClient::GetAnnotations(
    zx::duration timeout, const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  return device_id_provider_ptr_.GetId(timeout).then(
      [=](::fit::result<std::string, Error>& result) {
        Annotations annotations;
        if (result.is_error()) {
          annotations.insert({kAnnotationDeviceFeedbackId, AnnotationOr(result.error())});
        } else {
          annotations.insert({kAnnotationDeviceFeedbackId, AnnotationOr(result.take_value())});
        }

        return ::fit::ok(std::move(annotations));
      });
}

}  // namespace feedback_data
}  // namespace forensics
