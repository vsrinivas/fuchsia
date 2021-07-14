// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace feedback_data {
// Asks Scenic to take the screenshot of the current view and return it.
//
// fuchsia.ui.scenic.Scenic is expected to be in |services|.
::fpromise::promise<fuchsia::ui::scenic::ScreenshotData> TakeScreenshot(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout);

// Wraps around fuchsia::ui::scenic::ScenicPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// TakeScreenshot() is expected to be called only once.
class Scenic {
 public:
  Scenic(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fpromise::promise<fuchsia::ui::scenic::ScreenshotData> TakeScreenshot(fit::Timeout timeout);

 private:
  fidl::OneShotPtr<fuchsia::ui::scenic::Scenic, fuchsia::ui::scenic::ScreenshotData> scenic_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scenic);
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_
