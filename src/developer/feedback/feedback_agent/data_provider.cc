// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/developer/feedback/feedback_agent/annotations.h"
#include "src/developer/feedback/feedback_agent/attachments.h"
#include "src/developer/feedback/feedback_agent/attachments/screenshot_ptr.h"
#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/image_conversion.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::Data;
using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

// Timeout for a single asynchronous piece of data, e.g., syslog collection.
const zx::duration kDataTimeout = zx::sec(30);
// Timeout for requesting the screenshot from Scenic.
const zx::duration kScreenshotTimeout = zx::sec(10);

}  // namespace

std::unique_ptr<DataProvider> DataProvider::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    std::function<void()> after_timeout, zx::duration timeout) {
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

  return std::make_unique<DataProvider>(dispatcher, std::move(services), config, after_timeout,
                                        timeout);
}

DataProvider::DataProvider(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services, const Config& config,
                           std::function<void()> after_timeout, zx::duration timeout)
    : dispatcher_(dispatcher),
      services_(services),
      config_(config),
      after_timeout_(dispatcher, after_timeout, timeout),
      executor_(dispatcher),
      inspect_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      inspect_executor_(inspect_loop_.dispatcher()) {
  if (const zx_status_t status = inspect_loop_.StartThread("inspect-thread"); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Unable to start new thread for Inspect data collection";
  }
}

void DataProvider::GetData(GetDataCallback callback) {
  after_timeout_.Acquire();
  auto annotations =
      fit::join_promise_vector(
          GetAnnotations(dispatcher_, services_, config_.annotation_allowlist, kDataTimeout))
          .and_then([](std::vector<fit::result<std::vector<Annotation>>>& annotation_promises)
                        -> fit::result<std::vector<Annotation>> {
            std::vector<Annotation> ok_annotations;
            for (auto& promise : annotation_promises) {
              if (promise.is_ok()) {
                auto annotations = promise.take_value();
                for (const auto& annotation : annotations) {
                  ok_annotations.push_back(std::move(annotation));
                }
              }
            }

            if (ok_annotations.empty()) {
              return fit::error();
            }

            return fit::ok(ok_annotations);
          });

  auto attachments =
      fit::join_promise_vector(GetAttachments(dispatcher_, services_, config_.attachment_allowlist,
                                              kDataTimeout, &inspect_executor_))
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
              [](std::tuple<fit::result<std::vector<Annotation>>,
                            fit::result<std::vector<Attachment>>>& annotations_and_attachments) {
                Data data;

                auto& annotations_or_error = std::get<0>(annotations_and_attachments);
                if (annotations_or_error.is_ok()) {
                  data.set_annotations(annotations_or_error.take_value());
                } else {
                  FX_LOGS(WARNING) << "Failed to retrieve any annotations";
                }

                auto& attachments_or_error = std::get<1>(annotations_and_attachments);
                std::vector<Attachment> attachments;
                if (attachments_or_error.is_ok()) {
                  attachments = attachments_or_error.take_value();
                } else {
                  FX_LOGS(WARNING) << "Failed to retrieve any attachments";
                }

                // We also add the annotations as a single extra attachment.
                // This is useful for clients that surface the annotations differentily in the UI
                // but still want all the annotations to be easily downloadable in one file.
                if (data.has_annotations()) {
                  AddAnnotationsAsExtraAttachment(data.annotations(), &attachments);
                }

                // We bundle the attachments into a single attachment.
                // This is useful for most clients that want to pass around a single bundle.
                if (!attachments.empty()) {
                  Attachment bundle;
                  if (BundleAttachments(attachments, &bundle)) {
                    data.set_attachment_bundle(std::move(bundle));
                  }
                }

                return fit::ok(std::move(data));
              })
          .or_else([]() { return fit::error(ZX_ERR_INTERNAL); })
          .then([this, callback = std::move(callback)](fit::result<Data, zx_status_t>& result) {
            callback(std::move(result));
            after_timeout_.Release();
          });

  executor_.schedule_task(std::move(promise));
}

void DataProvider::GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) {
  after_timeout_.Acquire();
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
                     .then([this, callback = std::move(callback)](fit::result<Screenshot>& result) {
                       if (!result.is_ok()) {
                         callback(/*screenshot=*/nullptr);
                       } else {
                         callback(std::make_unique<Screenshot>(result.take_value()));
                       }
                       after_timeout_.Release();
                     });

  executor_.schedule_task(std::move(promise));
}

}  // namespace feedback
