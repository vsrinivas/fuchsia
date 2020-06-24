// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/channel_provider.h"

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationSystemUpdateChannelCurrent,
};

}  // namespace

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 cobalt::Logger* cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(cobalt) {}

::fit::promise<Annotations> ChannelProvider::GetAnnotations(zx::duration timeout,
                                                            const AnnotationKeys& allowlist) {
  if (RestrictAllowlist(allowlist, kSupportedAnnotations).empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  return fidl::GetCurrentChannel(
             dispatcher_, services_,
             fit::Timeout(
                 timeout,
                 /*action=*/
                 [cobalt = cobalt_] { cobalt->LogOccurrence(cobalt::TimedOutData::kChannel); }))
      .then([](const ::fit::result<std::string, Error>& result) {
        AnnotationOr annotation =
            (result.is_ok()) ? AnnotationOr(result.value()) : AnnotationOr(result.error());

        return ::fit::ok(Annotations({
            {kAnnotationSystemUpdateChannelCurrent, std::move(annotation)},
        }));
      });
}

}  // namespace feedback_data
}  // namespace forensics
