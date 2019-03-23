// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/feedback_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/developer/feedback_agent/annotations.h"
#include "src/developer/feedback_agent/attachments.h"
#include "src/developer/feedback_agent/image_conversion.h"

namespace fuchsia {
namespace feedback {

FeedbackAgent::FeedbackAgent(::sys::ComponentContext* startup_context)
    : context_(startup_context) {
  ConnectToScenic();
}

void FeedbackAgent::GetData(GetDataCallback callback) {
  DataProvider_GetData_Response response;
  response.data.set_annotations(GetAnnotations());
  response.data.set_attachments(GetAttachments());
  DataProvider_GetData_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

void FeedbackAgent::GetScreenshot(ImageEncoding encoding,
                                  GetScreenshotCallback callback) {
  // We add the provided callback to the vector of pending callbacks we maintain
  // and save a reference to pass to the Scenic callback.
  auto& saved_callback = get_png_screenshot_callbacks_.emplace_back(
      std::make_unique<GetScreenshotCallback>(std::move(callback)));

  // If we previously lost the connection to Scenic, we re-attempt to establish
  // it.
  if (!is_connected_to_scenic_) {
    ConnectToScenic();
  }

  scenic_->TakeScreenshot(
      [encoding, callback = saved_callback.get()](
          fuchsia::ui::scenic::ScreenshotData raw_screenshot, bool success) {
        if (!success) {
          FX_LOGS(ERROR) << "Scenic failed to take screenshot";
          (*callback)(/*screenshot=*/nullptr);
          return;
        }

        std::unique_ptr<Screenshot> screenshot = std::make_unique<Screenshot>();
        screenshot->dimensions_in_px.height = raw_screenshot.info.height;
        screenshot->dimensions_in_px.width = raw_screenshot.info.width;
        switch (encoding) {
          case ImageEncoding::PNG:
            if (!RawToPng(raw_screenshot.data, raw_screenshot.info.height,
                          raw_screenshot.info.width, raw_screenshot.info.stride,
                          raw_screenshot.info.pixel_format,
                          &screenshot->image)) {
              FX_LOGS(ERROR) << "Failed to convert raw screenshot to PNG";
              (*callback)(/*screenshot=*/nullptr);
              return;
            }
            break;
        }
        (*callback)(std::move(screenshot));
      });
}

void FeedbackAgent::ConnectToScenic() {
  scenic_ = context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Lost connection to Scenic service";
    is_connected_to_scenic_ = false;
    this->TerminateAllGetScreenshotCallbacks();
  });
  is_connected_to_scenic_ = true;
}

void FeedbackAgent::TerminateAllGetScreenshotCallbacks() {
  for (const auto& callback : get_png_screenshot_callbacks_) {
    (*callback)(/*screenshot=*/nullptr);
  }
  get_png_screenshot_callbacks_.clear();
}

}  // namespace feedback
}  // namespace fuchsia
