// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/channel_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using internal::ChannelProviderPtr;

}  // namespace

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
  auto channel_ptr = std::make_unique<ChannelProviderPtr>(dispatcher_, services_, cobalt_);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = channel_ptr->GetCurrent(timeout_);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                         /*args=*/std::move(channel_ptr))
      .and_then([](const AnnotationValue& channel) -> fit::result<Annotations> {
        return fit::ok(Annotations({{kAnnotationChannel, channel}}));
      })
      .or_else([] {
        FX_LOGS(WARNING) << "Failed to build annotation " << kAnnotationChannel;
        return fit::error();
      });
}

namespace internal {

ChannelProviderPtr::ChannelProviderPtr(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<sys::ServiceDirectory> services,
                                       Cobalt* cobalt)
    : services_(services),
      cobalt_(cobalt),

      bridge_(dispatcher, "Current OTA channel collection") {}

fit::promise<AnnotationValue> ChannelProviderPtr::GetCurrent(zx::duration timeout) {
  FXL_CHECK(!has_called_get_current_) << "GetCurrent() is not intended to be called twice";
  has_called_get_current_ = true;

  update_info_ = services_->Connect<fuchsia::update::channel::Provider>();

  update_info_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.update.channel.Provider";
    bridge_.CompleteError();
  });

  update_info_->GetCurrent([this](std::string channel) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    bridge_.CompleteOk(std::move(channel));
  });

  return bridge_.WaitForDone(
      timeout,
      /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kChannel); });
}

}  // namespace internal

}  // namespace feedback
