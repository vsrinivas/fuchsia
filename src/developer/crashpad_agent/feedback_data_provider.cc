// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/feedback_data_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace fuchsia {
namespace crash {

FeedbackDataProvider::FeedbackDataProvider(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<fuchsia::feedback::Data> FeedbackDataProvider::GetData(
    zx::duration timeout) {
  data_provider_ = services_->Connect<fuchsia::feedback::DataProvider>();
  done_ = std::make_shared<fit::bridge<fuchsia::feedback::Data>>();

  // fit::promise does not have the notion of a timeout. So we post a delayed
  // task that will call the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when
  // the fit::bridge is completed another way.
  done_after_timeout_.Reset([done = done_] {
    if (!done->completer) {
      return;
    }

    FX_LOGS(ERROR) << "Feedback data collection timed out";
    done->completer.complete_error();
  });
  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping Feedback data collection as it is not safe "
                      "without a timeout";
    return fit::make_result_promise<fuchsia::feedback::Data>(fit::error());
  }

  data_provider_.set_error_handler([this](zx_status_t status) {
    if (!done_->completer) {
      return;
    }

    FX_PLOGS(ERROR, status)
        << "Lost connection to fuchsia.feedback.DataProvider";
    done_->completer.complete_error();
    done_after_timeout_.Cancel();
  });

  data_provider_->GetData(
      [this](fuchsia::feedback::DataProvider_GetData_Result out_result) {
        if (!done_->completer) {
          return;
        }

        if (out_result.is_err()) {
          FX_PLOGS(WARNING, out_result.err())
              << "Failed to fetch feedback data";
          done_->completer.complete_error();
        } else {
          done_->completer.complete_ok(std::move(out_result.response().data));
        }
        done_after_timeout_.Cancel();
      });

  return done_->consumer.promise_or(fit::error());
}

}  // namespace crash
}  // namespace fuchsia
