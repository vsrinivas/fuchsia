// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/channel_provider.h"

#include <algorithm>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationSystemUpdateChannelCurrent,
    kAnnotationSystemUpdateChannelTarget,
};

}  // namespace

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 cobalt::Logger* cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(cobalt) {}

::fpromise::promise<Annotations> ChannelProvider::GetAnnotations(zx::duration timeout,
                                                                 const AnnotationKeys& allowlist) {
  const AnnotationKeys to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  using Result = ::fpromise::result<std::string, Error>;
  return ::fpromise::join_promises(
             fidl::GetCurrentChannel(dispatcher_, services_, fit::Timeout(timeout)),
             fidl::GetTargetChannel(dispatcher_, services_, fit::Timeout(timeout)))
      .and_then([this, to_get](std::tuple<Result, Result>& results) {
        Annotations annotations({
            {kAnnotationSystemUpdateChannelCurrent, std::get<0>(results)},
            {kAnnotationSystemUpdateChannelTarget, std::get<1>(results)},
        });

        if (std::find_if(annotations.begin(), annotations.end(), [](const auto& annotation) {
              const auto& value = annotation.second;
              return !value.HasValue() && value.Error() == Error::kTimeout;
            }) != annotations.end()) {
          cobalt_->LogOccurrence(cobalt::TimedOutData::kChannel);
        }

        return ::fpromise::ok(ExtractAllowlisted(to_get, annotations));
      });
}

}  // namespace feedback_data
}  // namespace forensics
