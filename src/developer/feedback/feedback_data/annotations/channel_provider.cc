// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/channel_provider.h"

#include "src/developer/feedback/feedback_data/annotations/utils.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/feedback/utils/fidl/channel_provider_ptr.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationSystemUpdateChannelCurrent,
};

}  // namespace

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 zx::duration timeout, Cobalt* cobalt)
    : dispatcher_(dispatcher), services_(services), timeout_(timeout), cobalt_(cobalt) {}

::fit::promise<Annotations> ChannelProvider::GetAnnotations(const AnnotationKeys& allowlist) {
  if (RestrictAllowlist(allowlist, kSupportedAnnotations).empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  return fidl::GetCurrentChannel(
             dispatcher_, services_,
             fit::Timeout(
                 timeout_,
                 /*action=*/[cobalt = cobalt_] { cobalt->LogOccurrence(TimedOutData::kChannel); }))
      .and_then([](const AnnotationValue& channel) -> ::fit::result<Annotations> {
        return ::fit::ok(Annotations({{kAnnotationSystemUpdateChannelCurrent, channel}}));
      })
      .or_else([] {
        FX_LOGS(WARNING) << "Failed to build annotation " << kAnnotationSystemUpdateChannelCurrent;
        return ::fit::error();
      });
}

}  // namespace feedback
