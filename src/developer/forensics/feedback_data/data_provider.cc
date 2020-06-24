// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/attachments/screenshot_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/attachments/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/image_conversion.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::Bugreport;
using fuchsia::feedback::Data;
using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;

// Timeout for a single asynchronous piece of data, e.g., syslog collection if the client didn't
// specify one.
const zx::duration kDefaultDataTimeout = zx::sec(30);

// Timeout for requesting the screenshot from Scenic.
const zx::duration kScreenshotTimeout = zx::sec(10);

}  // namespace

DataProvider::DataProvider(async_dispatcher_t* dispatcher,
                           std::shared_ptr<sys::ServiceDirectory> services,
                           IntegrityReporter integrity_reporter, cobalt::Logger* cobalt,
                           Datastore* datastore)
    : dispatcher_(dispatcher),
      services_(services),
      integrity_reporter_(integrity_reporter),
      cobalt_(cobalt),
      datastore_(datastore),
      executor_(dispatcher_) {}

void DataProvider::GetBugreport(fuchsia::feedback::GetBugreportParameters params,
                                GetBugreportCallback callback) {
  const zx::duration timeout = (params.has_collection_timeout_per_data())
                                   ? zx::duration(params.collection_timeout_per_data())
                                   : kDefaultDataTimeout;

  const uint64_t timer_id = cobalt_->StartTimer();

  auto promise =
      ::fit::join_promises(datastore_->GetAnnotations(timeout), datastore_->GetAttachments(timeout))
          .and_then([this](std::tuple<::fit::result<Annotations>, ::fit::result<Attachments>>&
                               annotations_and_attachments) {
            Bugreport bugreport;
            std::vector<fuchsia::feedback::Attachment> attachments;

            const auto& annotations_result = std::get<0>(annotations_and_attachments);
            if (annotations_result.is_ok()) {
              bugreport.set_annotations(ToFeedbackAnnotationVector(annotations_result.value()));
            } else {
              FX_LOGS(WARNING) << "Failed to retrieve any annotations";
            }

            const auto& attachments_result = std::get<1>(annotations_and_attachments);
            if (attachments_result.is_ok()) {
              attachments = ToFeedbackAttachmentVector(attachments_result.value());
            } else {
              FX_LOGS(WARNING) << "Failed to retrieve any attachments";
            }

            // We also add the annotations as a single extra attachment.
            // This is useful for clients that surface the annotations differentily in the UI
            // but still want all the annotations to be easily downloadable in one file.
            if (bugreport.has_annotations()) {
              const auto annotations_json = ToJsonString(bugreport.annotations());
              if (annotations_json.has_value()) {
                AddToAttachments(kAttachmentAnnotations, annotations_json.value(), &attachments);
              }
            }

            if (const auto integrity_report = integrity_reporter_.MakeIntegrityReport(
                    annotations_result, attachments_result,
                    datastore_->IsMissingNonPlatformAnnotations());
                integrity_report.has_value()) {
              AddToAttachments(kAttachmentManifest, integrity_report.value(), &attachments);
            }

            // We bundle the attachments into a single attachment.
            // This is useful for most clients that want to pass around a single bundle.
            if (!attachments.empty()) {
              fuchsia::feedback::Attachment bundle;
              if (BundleAttachments(attachments, &bundle)) {
                bugreport.set_bugreport(std::move(bundle));
              }
            }

            return ::fit::ok(std::move(bugreport));
          })
          .then([this, callback = std::move(callback), timer_id](::fit::result<Bugreport>& result) {
            if (result.is_error()) {
              cobalt_->LogElapsedTime(cobalt::BugreportGenerationFlow::kFailure, timer_id);
              callback(Bugreport());
            } else {
              cobalt_->LogElapsedTime(cobalt::BugreportGenerationFlow::kSuccess, timer_id);
              callback(result.take_value());
            }
          });

  executor_.schedule_task(std::move(promise));
}

void DataProvider::GetData(GetDataCallback callback) {
  GetBugreport(fuchsia::feedback::GetBugreportParameters(),
               [callback = std::move(callback)](Bugreport bugreport) {
                 Data data;
                 if (bugreport.has_annotations()) {
                   data.set_annotations(bugreport.annotations());
                 }
                 if (bugreport.has_bugreport()) {
                   data.set_attachment_bundle(std::move(*bugreport.mutable_bugreport()));
                 }
                 callback(::fit::ok(std::move(data)));
               });
}

void DataProvider::GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) {
  auto promise =
      TakeScreenshot(
          dispatcher_, services_,
          fit::Timeout(kScreenshotTimeout,
                       [this] { cobalt_->LogOccurrence(cobalt::TimedOutData::kScreenshot); }))
          .and_then([encoding](fuchsia::ui::scenic::ScreenshotData& raw_screenshot)
                        -> ::fit::result<Screenshot> {
            Screenshot screenshot;
            screenshot.dimensions_in_px.height = raw_screenshot.info.height;
            screenshot.dimensions_in_px.width = raw_screenshot.info.width;
            switch (encoding) {
              case ImageEncoding::PNG:
                if (!RawToPng(raw_screenshot.data, raw_screenshot.info.height,
                              raw_screenshot.info.width, raw_screenshot.info.stride,
                              raw_screenshot.info.pixel_format, &screenshot.image)) {
                  FX_LOGS(ERROR) << "Failed to convert raw screenshot to PNG";
                  return ::fit::error();
                }
                break;
            }
            return ::fit::ok(std::move(screenshot));
          })
          .then([callback = std::move(callback)](::fit::result<Screenshot>& result) {
            if (!result.is_ok()) {
              callback(/*screenshot=*/nullptr);
            } else {
              callback(std::make_unique<Screenshot>(result.take_value()));
            }
          });

  executor_.schedule_task(std::move(promise));
}

}  // namespace feedback_data
}  // namespace forensics
