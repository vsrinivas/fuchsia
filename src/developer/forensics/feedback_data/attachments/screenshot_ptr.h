// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/utils/errors.h"

namespace forensics::feedback_data {

// Asks Scenic to take the screenshot of the current view and return it.
//
// fuchsia.ui.scenic.Scenic is expected to be in |services|.
::fpromise::promise<fuchsia::ui::scenic::ScreenshotData, Error> TakeScreenshot(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    zx::duration timeout);

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_SCREENSHOT_PTR_H_
