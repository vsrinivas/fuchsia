// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/channel_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zircon/errors.h>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using internal::ChannelProviderPtr;

}  // namespace

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 zx::duration timeout, std::shared_ptr<Cobalt> cobalt)
    : dispatcher_(dispatcher), services_(services), timeout_(timeout), cobalt_(std::move(cobalt)) {}

std::set<std::string> ChannelProvider::GetSupportedAnnotations() {
  return {
      kAnnotationChannel,
  };
}

fit::promise<std::vector<Annotation>> ChannelProvider::GetAnnotations() {
  auto channel_ptr = std::make_unique<ChannelProviderPtr>(dispatcher_, services_, cobalt_);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = channel_ptr->GetCurrent(timeout_);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                         /*args=*/std::move(channel_ptr))
      .and_then([](const std::string& channel) {
        std::vector<Annotation> annotations;

        Annotation annotation;
        annotation.key = kAnnotationChannel;
        annotation.value = channel;

        annotations.push_back(std::move(annotation));
        return fit::ok(std::move(annotations));
      })
      .or_else([] {
        FX_LOGS(WARNING) << "Failed to build annotation " << kAnnotationChannel;
        return fit::error();
      });
}

namespace internal {

ChannelProviderPtr::ChannelProviderPtr(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<sys::ServiceDirectory> services,
                                       std::shared_ptr<Cobalt> cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(std::move(cobalt)) {}

fit::promise<std::string> ChannelProviderPtr::GetCurrent(zx::duration timeout) {
  FXL_CHECK(!has_called_get_current_) << "GetCurrent() is not intended to be called twice";
  has_called_get_current_ = true;

  update_info_ = services_->Connect<fuchsia::update::channel::Provider>();

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when the fit::bridge is
  // completed another way.
  //
  // It is safe to pass "this" to the fit::function as the callback won't be callable when the
  // CancelableClosure goes out of scope, which is before "this".
  done_after_timeout_.Reset([this] {
    if (!done_.completer) {
      return;
    }

    FX_LOGS(ERROR) << "Current OTA channel retrieval timed out";
    cobalt_->LogOccurrence(TimedOutData::kChannel);
    done_.completer.complete_error();
  });
  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping current OTA channel retrieval as it is not safe without a timeout";
    return fit::make_result_promise<std::string>(fit::error());
  }

  update_info_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.update.channel.Provider";
    done_.completer.complete_error();
  });

  update_info_->GetCurrent([this](std::string channel) {
    if (!done_.completer) {
      return;
    }

    done_.completer.complete_ok(std::move(channel));
  });

  return done_.consumer.promise_or(fit::error()).then([this](fit::result<std::string>& result) {
    done_after_timeout_.Cancel();
    return std::move(result);
  });
}

}  // namespace internal

}  // namespace feedback
