// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/screenshot_ptr.h"

#include <lib/async/cpp/task.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

}  // namespace

fit::promise<ScreenshotData> TakeScreenshot(async_dispatcher_t* dispatcher,
                                            std::shared_ptr<sys::ServiceDirectory> services,
                                            zx::duration timeout) {
  std::unique_ptr<Scenic> scenic = std::make_unique<Scenic>(dispatcher, services);

  // We move |scenic| in a subsequent chained promise to guarantee its lifetime.
  return scenic->TakeScreenshot(timeout).then(
      [scenic = std::move(scenic)](fit::result<ScreenshotData>& result) {
        return std::move(result);
      });
}

Scenic::Scenic(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher), services_(services) {}

fit::promise<ScreenshotData> Scenic::TakeScreenshot(const zx::duration timeout) {
  FXL_CHECK(!has_called_take_screenshot_) << "TakeScreenshot() is not intended to be called twice";
  has_called_take_screenshot_ = true;

  scenic_ = services_->Connect<fuchsia::ui::scenic::Scenic>();

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

    FX_LOGS(ERROR) << "Screenshot take timed out";
    done_.completer.complete_error();
  });
  const zx_status_t post_status = async::PostDelayedTask(
      dispatcher_, [cb = done_after_timeout_.callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_PLOGS(ERROR, post_status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping screenshot take as it is not safe without a timeout";
    return fit::make_result_promise<ScreenshotData>(fit::error());
  }

  scenic_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.ui.scenic.Scenic";
    done_.completer.complete_error();
  });

  scenic_->TakeScreenshot([this](ScreenshotData raw_screenshot, bool success) {
    if (!done_.completer) {
      return;
    }

    if (!success) {
      FX_LOGS(ERROR) << "Scenic failed to take screenshot";
      done_.completer.complete_error();
    } else {
      done_.completer.complete_ok(std::move(raw_screenshot));
    }
  });

  return done_.consumer.promise_or(fit::error()).then([this](fit::result<ScreenshotData>& result) {
    done_after_timeout_.Cancel();
    return std::move(result);
  });
}

}  // namespace feedback
