// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/developer/feedback/feedback_agent/annotations.h"
#include "src/developer/feedback/feedback_agent/attachments.h"
#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/image_conversion.h"
#include "src/developer/feedback/feedback_agent/scenic_ptr.h"
#include "src/lib/files/file.h"

namespace fuchsia {
namespace feedback {
namespace {

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

// Timeout for a single asynchronous piece of data, e.g., syslog collection.
const zx::duration kDataTimeout = zx::sec(10);
// Timeout for requesting the screenshot from Scenic.
const zx::duration kScreenshotTimeout = zx::sec(10);

}  // namespace

std::unique_ptr<DataProviderImpl> DataProviderImpl::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<::sys::ServiceDirectory> services) {
  Config config;

  // We use the default config included in the package of this component if no override config was
  // specified or if we failed to parse the override config.
  bool use_default_config = true;

  if (files::IsFile(kOverrideConfigPath)) {
    use_default_config = false;
    if (const zx_status_t status = ParseConfig(kOverrideConfigPath, &config); status != ZX_OK) {
      // We failed to parse the override config: fall back to the default config.
      use_default_config = true;
      FX_PLOGS(ERROR, status) << "Failed to read override config file at " << kOverrideConfigPath
                              << " - falling back to default config file";
    }
  }

  // Either there was no override config or we failed to parse it.
  if (use_default_config) {
    if (const zx_status_t status = ParseConfig(kDefaultConfigPath, &config); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to read default config file at " << kDefaultConfigPath;

      FX_LOGS(FATAL) << "Failed to set up data provider";
      return nullptr;
    }
  }

  return std::make_unique<DataProviderImpl>(dispatcher, std::move(services), config);
}

DataProviderImpl::DataProviderImpl(async_dispatcher_t* dispatcher,
                                   std::shared_ptr<::sys::ServiceDirectory> services,
                                   const Config& config)
    : dispatcher_(dispatcher), executor_(dispatcher), services_(services), config_(config) {}

void DataProviderImpl::GetData(GetDataCallback callback) {
  auto annotations =
      fit::join_promise_vector(
          GetAnnotations(dispatcher_, services_, config_.annotation_allowlist, kDataTimeout))
          .and_then([](std::vector<fit::result<Annotation>>& annotations)
                        -> fit::result<std::vector<Annotation>> {
            std::vector<Annotation> ok_annotations;
            for (auto& annotation : annotations) {
              if (annotation.is_ok()) {
                ok_annotations.emplace_back(annotation.take_value());
              }
            }

            if (ok_annotations.empty()) {
              return fit::error();
            }

            return fit::ok(ok_annotations);
          });

  auto attachments =
      fit::join_promise_vector(
          GetAttachments(dispatcher_, services_, config_.attachment_allowlist, kDataTimeout))
          .and_then([](std::vector<fit::result<Attachment>>& attachments)
                        -> fit::result<std::vector<Attachment>> {
            std::vector<Attachment> ok_attachments;
            for (auto& attachment : attachments) {
              if (attachment.is_ok()) {
                ok_attachments.emplace_back(attachment.take_value());
              }
            }

            if (ok_attachments.empty()) {
              return fit::error();
            }

            return fit::ok(std::move(ok_attachments));
          });

  auto promise =
      fit::join_promises(std::move(annotations), std::move(attachments))
          .and_then(
              [callback = std::move(callback)](
                  std::tuple<fit::result<std::vector<Annotation>>,
                             fit::result<std::vector<Attachment>>>& annotations_and_attachments) {
                DataProvider_GetData_Response response;

                auto& annotations = std::get<0>(annotations_and_attachments);
                if (annotations.is_ok()) {
                  response.data.set_annotations(annotations.take_value());
                } else {
                  FX_LOGS(WARNING) << "Failed to retrieve any annotations";
                }

                auto& attachments = std::get<1>(annotations_and_attachments);
                if (attachments.is_ok()) {
                  response.data.set_attachments(attachments.take_value());
                } else {
                  FX_LOGS(WARNING) << "Failed to retrieve any attachments";
                }

                DataProvider_GetData_Result result;
                result.set_response(std::move(response));
                callback(std::move(result));
              })
          .or_else([callback = std::move(callback)] {
            DataProvider_GetData_Result result;
            result.set_err(ZX_ERR_INTERNAL);
            callback(std::move(result));
          });

  executor_.schedule_task(std::move(promise));
}

void DataProviderImpl::GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) {
  auto promise = TakeScreenshot(dispatcher_, services_, kScreenshotTimeout)
                     .and_then([encoding](fuchsia::ui::scenic::ScreenshotData& raw_screenshot)
                                   -> fit::result<Screenshot> {
                       Screenshot screenshot;
                       screenshot.dimensions_in_px.height = raw_screenshot.info.height;
                       screenshot.dimensions_in_px.width = raw_screenshot.info.width;
                       switch (encoding) {
                         case ImageEncoding::PNG:
                           if (!RawToPng(raw_screenshot.data, raw_screenshot.info.height,
                                         raw_screenshot.info.width, raw_screenshot.info.stride,
                                         raw_screenshot.info.pixel_format, &screenshot.image)) {
                             FX_LOGS(ERROR) << "Failed to convert raw screenshot to PNG";
                             return fit::error();
                           }
                           break;
                       }
                       return fit::ok(std::move(screenshot));
                     })
                     .then([callback = std::move(callback)](fit::result<Screenshot>& result) {
                       if (!result.is_ok()) {
                         callback(/*screenshot=*/nullptr);
                       } else {
                         callback(std::make_unique<Screenshot>(result.take_value()));
                       }
                     });

  executor_.schedule_task(std::move(promise));
}

}  // namespace feedback
}  // namespace fuchsia
