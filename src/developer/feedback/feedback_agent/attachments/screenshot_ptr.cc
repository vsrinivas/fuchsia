// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/screenshot_ptr.h"

#include <lib/async/cpp/task.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

}  // namespace

fit::promise<ScreenshotData> TakeScreenshot(async_dispatcher_t* dispatcher,
                                            std::shared_ptr<sys::ServiceDirectory> services,
                                            zx::duration timeout, Cobalt* cobalt) {
  std::unique_ptr<Scenic> scenic = std::make_unique<Scenic>(dispatcher, services, cobalt);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto screenshot = scenic->TakeScreenshot(timeout);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(screenshot),
                                         /*args=*/std::move(scenic));
}

Scenic::Scenic(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               Cobalt* cobalt)
    : services_(services), cobalt_(cobalt), bridge_(dispatcher, "Screenshot collection") {}

fit::promise<ScreenshotData> Scenic::TakeScreenshot(const zx::duration timeout) {
  FXL_CHECK(!has_called_take_screenshot_) << "TakeScreenshot() is not intended to be called twice";
  has_called_take_screenshot_ = true;

  scenic_ = services_->Connect<fuchsia::ui::scenic::Scenic>();

  scenic_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.ui.scenic.Scenic";
    bridge_.CompleteError();
  });

  scenic_->TakeScreenshot([this](ScreenshotData raw_screenshot, bool success) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    if (!success) {
      FX_LOGS(ERROR) << "Scenic failed to take screenshot";
      bridge_.CompleteError();
    } else {
      bridge_.CompleteOk(std::move(raw_screenshot));
    }
  });

  return bridge_.WaitForDone(
      timeout,
      /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kScreenshot); });
}

}  // namespace feedback
