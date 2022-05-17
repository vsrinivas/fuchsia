// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/annotation_provider_factory.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <vector>

#include "src/developer/forensics/feedback_data/annotations/device_id_provider.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics {
namespace feedback_data {

// Get the annotation providers that will collect the annotations in |allowlist_|.
std::vector<std::unique_ptr<AnnotationProvider>> GetReusableProviders(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    feedback::DeviceIdProvider* device_id_provider) {
  std::vector<std::unique_ptr<AnnotationProvider>> providers;

  providers.push_back(std::make_unique<DeviceIdProviderClient>(device_id_provider));

  return providers;
}

}  // namespace feedback_data
}  // namespace forensics
