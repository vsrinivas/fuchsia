// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/screenshot.h"

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl_oneshot.h"

namespace forensics::feedback_data {
namespace {

using fuchsia::ui::scenic::ScreenshotData;

}  // namespace

::fpromise::promise<ScreenshotData, Error> TakeScreenshot(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    zx::duration timeout) {
  return OneShotCall<fuchsia::ui::scenic::Scenic, &fuchsia::ui::scenic::Scenic::TakeScreenshot>(
             dispatcher, services, timeout)
      .and_then([](std::tuple<ScreenshotData, bool>& result)
                    -> ::fpromise::result<ScreenshotData, Error> {
        auto& [data, success] = result;
        if (success) {
          return ::fpromise::ok(std::move(data));
        }

        FX_LOGS(WARNING) << "Scenic failed to take screenshot";
        return ::fpromise::error(Error::kDefault);
      });
}

}  // namespace forensics::feedback_data
