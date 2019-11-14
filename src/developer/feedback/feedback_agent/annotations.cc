// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations.h"

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider_factory.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
using fuchsia::feedback::Annotation;

std::vector<fit::promise<std::vector<Annotation>>> GetAnnotations(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const std::set<std::string>& allowlist, zx::duration timeout) {
  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Annotation allowlist is empty, nothing to retrieve";
    return {};
  }

  std::vector<fit::promise<std::vector<Annotation>>> annotation_promises;

  for (auto& provider : GetProviders(allowlist, dispatcher, services, timeout)) {
    annotation_promises.push_back(provider->GetAnnotations());
  }

  return annotation_promises;
}

}  // namespace feedback
