// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/datastore.h"

#include <lib/fit/promise.h>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider_factory.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

Datastore::Datastore(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt,
                     const zx::duration timeout, const AnnotationKeys& annotation_allowlist)
    : dispatcher_(dispatcher),
      services_(services),
      cobalt_(cobalt),
      timeout_(timeout),
      annotation_allowlist_(annotation_allowlist) {
  if (annotation_allowlist_.empty()) {
    FX_LOGS(WARNING)
        << "Annotation allowlist is empty, no annotations will be collected or returned";
  }
}

fit::promise<Annotations> Datastore::GetAnnotations() {
  if (annotation_allowlist_.empty()) {
    return fit::make_result_promise<Annotations>(fit::error());
  }

  std::vector<fit::promise<Annotations>> annotations;
  for (auto& provider :
       GetProviders(annotation_allowlist_, dispatcher_, services_, timeout_, cobalt_)) {
    annotations.push_back(provider->GetAnnotations());
  }

  return fit::join_promise_vector(std::move(annotations))
      .and_then([](std::vector<fit::result<Annotations>>& annotations) -> fit::result<Annotations> {
        Annotations ok_annotations;
        for (auto& promise : annotations) {
          if (promise.is_ok()) {
            for (const auto& [key, value] : promise.take_value()) {
              ok_annotations[key] = value;
            }
          }
        }

        if (ok_annotations.empty()) {
          return fit::error();
        }

        return fit::ok(ok_annotations);
      });
}

}  // namespace feedback
