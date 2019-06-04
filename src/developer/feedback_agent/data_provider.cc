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
#include "src/developer/feedback_agent/config.h"
#include "src/developer/feedback_agent/image_conversion.h"

namespace fuchsia {
namespace feedback {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";

}  // namespace

std::unique_ptr<DataProviderImpl> DataProviderImpl::TryCreate(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services) {
  Config config;

  const zx_status_t parse_status = ParseConfig(kDefaultConfigPath, &config);
  if (parse_status != ZX_OK) {
    FX_PLOGS(ERROR, parse_status)
        << "Failed to read default config file at " << kDefaultConfigPath;
    FX_LOGS(FATAL) << "Failed to set up data provider";
    return nullptr;
  }

  return std::make_unique<DataProviderImpl>(dispatcher, std::move(services),
                                            config);
}

DataProviderImpl::DataProviderImpl(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services, const Config& config)
    : executor_(dispatcher), services_(services), config_(config) {}

void DataProviderImpl::GetData(GetDataCallback callback) {
  // Today attachments are fetched asynchronously, but annotations are not.
  // In the future, we can use fit::join_promises() if annotations need to be
  // fetched asynchronously as well.
  const auto annotations = GetAnnotations(config_.annotation_allowlist);

  auto promise =
      fit::join_promise_vector(
          GetAttachments(services_, config_.attachment_allowlist))
          .then([annotations = std::move(annotations),
                 callback = std::move(callback)](
                    fit::result<std::vector<fit::result<Attachment>>>&
                        attachments) {
            DataProvider_GetData_Response response;
            if (!annotations.empty()) {
              response.data.set_annotations(annotations);
            }

            if (attachments.is_ok()) {
              std::vector<Attachment> ok_attachments;
              for (auto& attachment : attachments.take_value()) {
                if (attachment.is_ok()) {
                  ok_attachments.emplace_back(attachment.take_value());
                }
              }

              if (!ok_attachments.empty()) {
                response.data.set_attachments(std::move(ok_attachments));
              }
            } else {
              FX_LOGS(WARNING) << "Failed to retrieve any attachments";
            }

            DataProvider_GetData_Result result;
            result.set_response(std::move(response));
            callback(std::move(result));
          });

  executor_.schedule_task(std::move(promise));
}

void DataProviderImpl::GetScreenshot(ImageEncoding encoding,
                                     GetScreenshotCallback callback) {
  // We wrap the callback in a shared_ptr to share it between the error handler
  // of the FIDL connection and the Scenic::TakeScreenshot() callback.
  std::shared_ptr<GetScreenshotCallback> shared_callback =
      std::make_shared<GetScreenshotCallback>(std::move(callback));

  const uint64_t id = next_scenic_id_++;

  scenics_[id] = services_->Connect<fuchsia::ui::scenic::Scenic>();
  scenics_[id].set_error_handler(
      [this, id, shared_callback](zx_status_t status) {
        CloseScenic(id);

        FX_PLOGS(ERROR, status) << "Lost connection to Scenic service";
        (*shared_callback)(/*screenshot=*/nullptr);
      });
  scenics_[id]->TakeScreenshot(
      // We pass |scenic| to the lambda to keep it alive.
      [this, id, encoding, shared_callback](
          fuchsia::ui::scenic::ScreenshotData raw_screenshot, bool success) {
        CloseScenic(id);

        if (!success) {
          FX_LOGS(ERROR) << "Scenic failed to take screenshot";
          (*shared_callback)(/*screenshot=*/nullptr);
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
              (*shared_callback)(/*screenshot=*/nullptr);
              return;
            }
            break;
        }
        (*shared_callback)(std::move(screenshot));
      });
}

void DataProviderImpl::CloseScenic(const uint64_t id) {
  if (scenics_.erase(id) == 0) {
    FX_LOGS(ERROR) << "No fuchsia.ui.scenic.Scenic connection to close with id "
                   << id;
  }
}

}  // namespace feedback
}  // namespace fuchsia
