// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations.h"

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider_factory.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
using fuchsia::feedback::Annotation;

std::vector<fit::promise<Annotation>> GetAnnotations(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const std::set<std::string>& allowlist, zx::duration timeout) {
  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Annotation allowlist is empty, nothing to retrieve";
    return {};
  }

  std::vector<fit::promise<Annotation>> annotations;

  for (auto& provider : GetProviders(allowlist, dispatcher, services, timeout)) {
    auto new_annotations = provider->GetAnnotations();
    annotations.insert(annotations.end(), std::make_move_iterator(new_annotations.begin()),
                       std::make_move_iterator(new_annotations.end()));
  }

  return annotations;
}

}  // namespace feedback
