// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/screenshot_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

}  // namespace

::fit::promise<ScreenshotData> TakeScreenshot(async_dispatcher_t* dispatcher,
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              fit::Timeout timeout) {
  std::unique_ptr<Scenic> scenic = std::make_unique<Scenic>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto screenshot = scenic->TakeScreenshot(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(screenshot),
                                              /*args=*/std::move(scenic));
}

Scenic::Scenic(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : scenic_(dispatcher, services) {}

::fit::promise<ScreenshotData> Scenic::TakeScreenshot(fit::Timeout timeout) {
  scenic_->TakeScreenshot([this](ScreenshotData raw_screenshot, bool success) {
    if (scenic_.IsAlreadyDone()) {
      return;
    }

    if (!success) {
      FX_LOGS(WARNING) << "Scenic failed to take screenshot";
      scenic_.CompleteError(Error::kDefault);
    } else {
      scenic_.CompleteOk(std::move(raw_screenshot));
    }
  });

  return scenic_.WaitForDone(std::move(timeout)).or_else([](const Error& error) {
    return ::fit::error();
  });
}

}  // namespace feedback_data
}  // namespace forensics
