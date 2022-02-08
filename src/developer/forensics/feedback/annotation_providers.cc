// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotation_providers.h"

#include <memory>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics::feedback {

AnnotationProviders::AnnotationProviders(async_dispatcher_t* dispatcher,
                                         std::set<std::string> allowlist,
                                         Annotations static_annotations)
    : dispatcher_(dispatcher),
      data_register_(kMaxNumNonPlatformAnnotations, kReservedAnnotationNamespaces,
                     kDataRegisterPath),
      time_provider_(std::make_unique<timekeeper::SystemClock>()),
      annotation_manager_(allowlist, static_annotations, &data_register_, {&time_provider_}) {}

void AnnotationProviders::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  data_register_connections_.AddBinding(&data_register_, std::move(request), dispatcher_,
                                        std::move(error_handler));
}

}  // namespace forensics::feedback
