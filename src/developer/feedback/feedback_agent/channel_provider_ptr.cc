// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/channel_provider_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace feedback {

fit::promise<std::string> RetrieveCurrentChannel(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<::sys::ServiceDirectory> services,
                                                 zx::duration timeout) {
  std::unique_ptr<UpdateInfo> update_info = std::make_unique<UpdateInfo>(dispatcher, services);

  // We move |update_info| in a subsequent chained promise to guarantee its lifetime.
  return update_info->GetChannel(timeout).then(
      [update_info = std::move(update_info)](fit::result<std::string>& result) {
        return std::move(result);
      });
}

UpdateInfo::UpdateInfo(async_dispatcher_t* dispatcher,
                       std::shared_ptr<::sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<std::string> UpdateInfo::GetChannel(zx::duration timeout) {
  FXL_CHECK(!has_called_get_channel_) << "GetChannel() is not intended to be called twice";
  has_called_get_channel_ = true;

  update_info_ = services_->Connect<fuchsia::update::Info>();

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

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.update.Info";
    done_.completer.complete_error();
  });

  update_info_->GetChannel([this](std::string channel) {
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

}  // namespace feedback
}  // namespace fuchsia
