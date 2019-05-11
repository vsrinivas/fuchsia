// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/developer/feedback_agent/annotations.h"
#include "src/developer/feedback_agent/attachments.h"
#include "src/developer/feedback_agent/image_conversion.h"

namespace fuchsia {
namespace feedback {

DataProviderImpl::DataProviderImpl(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services)
    : executor_(dispatcher), services_(services) {}

void DataProviderImpl::GetData(GetDataCallback callback) {
  // Today attachments are fetched asynchronously, but annotations are not.
  // In the future, we can use fit::join_promises() if annotations need to be
  // fetched asynchronously as well.
  auto promise =
      fit::join_promise_vector(GetAttachments(services_))
          .then([callback = std::move(callback)](
                    fit::result<std::vector<fit::result<Attachment>>>&
                        attachments) {
            DataProvider_GetData_Response response;
            response.data.set_annotations(GetAnnotations());

            if (attachments.is_ok()) {
              std::vector<Attachment> ok_attachments;
              for (auto& attachment : attachments.take_value()) {
                if (attachment.is_ok()) {
                  ok_attachments.emplace_back(attachment.take_value());
                }
              }
              response.data.set_attachments(std::move(ok_attachments));
            } else {
              FX_LOGS(WARNING) << "failed to retrieve any attachments";
            }

            DataProvider_GetData_Result result;
            result.set_response(std::move(response));
            callback(std::move(result));
          });

  executor_.schedule_task(std::move(promise));
}

void DataProviderImpl::GetScreenshot(ImageEncoding encoding,
                                     GetScreenshotCallback callback) {
  // We add the provided callback to the vector of pending callbacks we maintain
  // and save a reference to pass to the Scenic callback.
  auto& saved_callback = get_png_screenshot_callbacks_.emplace_back(
      std::make_unique<GetScreenshotCallback>(std::move(callback)));

  // If we previously lost the connection to Scenic or never connected to
  // Scenic, we (re-)attempt to establish the connection.
  if (!scenic_) {
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

void DataProviderImpl::ConnectToScenic() {
  scenic_ = services_->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Lost connection to Scenic service";
    this->TerminateAllGetScreenshotCallbacks();
  });
}

void DataProviderImpl::TerminateAllGetScreenshotCallbacks() {
  for (const auto& callback : get_png_screenshot_callbacks_) {
    (*callback)(/*screenshot=*/nullptr);
  }
  get_png_screenshot_callbacks_.clear();
}

}  // namespace feedback
}  // namespace fuchsia
