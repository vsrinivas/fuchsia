// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/channel_provider.h"

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/fidl/channel_provider_ptr.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 zx::duration timeout, Cobalt* cobalt)
    : dispatcher_(dispatcher), services_(services), timeout_(timeout), cobalt_(cobalt) {}

AnnotationKeys ChannelProvider::GetSupportedAnnotations() {
  return {
      kAnnotationChannel,
  };
}

fit::promise<Annotations> ChannelProvider::GetAnnotations() {
  auto ptr = std::make_unique<fidl::ChannelProviderPtr>(dispatcher_, services_);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = ptr->GetCurrentChannel(timeout_, /*if_timeout=*/[cobalt = cobalt_] {
    cobalt->LogOccurrence(TimedOutData::kChannel);
  });
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                         /*args=*/std::move(ptr))
      .and_then([](const AnnotationValue& channel) -> fit::result<Annotations> {
        return fit::ok(Annotations({{kAnnotationChannel, channel}}));
      })
      .or_else([] {
        FX_LOGS(WARNING) << "Failed to build annotation " << kAnnotationChannel;
        return fit::error();
      });
}

}  // namespace feedback
