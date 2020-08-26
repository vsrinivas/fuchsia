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

#include <map>
#include <memory>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/attachments/screenshot_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/image_conversion.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;
using fuchsia::feedback::Snapshot;

// Timeout for a single asynchronous piece of data, e.g., syslog collection, if the client didn't
// specify one.
//
// 30s seems reasonable to collect everything.
const zx::duration kDefaultDataTimeout = zx::sec(30);

// Timeout for requesting the screenshot from Scenic.
//
// 10 seconds seems reasonable to take a screenshot.
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
  fuchsia::feedback::GetSnapshotParameters new_params;
  if (params.has_collection_timeout_per_data()) {
    new_params.set_collection_timeout_per_data(params.collection_timeout_per_data());
  }
  GetSnapshot(std::move(new_params), [callback = std::move(callback)](Snapshot snapshot) {
    fuchsia::feedback::Bugreport bugreport;
    if (snapshot.has_annotations()) {
      bugreport.set_annotations(snapshot.annotations());
    }
    if (snapshot.has_archive()) {
      bugreport.set_bugreport(std::move(*snapshot.mutable_archive()));
    }
    callback(std::move(bugreport));
  });
}

void DataProvider::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                               GetSnapshotCallback callback) {
  const zx::duration timeout = (params.has_collection_timeout_per_data())
                                   ? zx::duration(params.collection_timeout_per_data())
                                   : kDefaultDataTimeout;

  const uint64_t timer_id = cobalt_->StartTimer();
  auto promise =
      ::fit::join_promises(datastore_->GetAnnotations(timeout), datastore_->GetAttachments(timeout))
          .and_then([this](std::tuple<::fit::result<Annotations>, ::fit::result<Attachments>>&
                               annotations_and_attachments) {
            Snapshot snapshot;
            std::map<std::string, std::string> attachments;

            const auto& annotations_result = std::get<0>(annotations_and_attachments);
            if (annotations_result.is_ok()) {
              snapshot.set_annotations(ToFeedbackAnnotationVector(annotations_result.value()));
            } else {
              FX_LOGS(WARNING) << "Failed to retrieve any annotations";
            }

            const auto& attachments_result = std::get<1>(annotations_and_attachments);
            if (attachments_result.is_ok()) {
              for (const auto& [key, value] : attachments_result.value()) {
                if (value.HasValue()) {
                  attachments[key] = value.Value();
                }
              }
            } else {
              FX_LOGS(WARNING) << "Failed to retrieve any attachments";
            }

            // We also add the annotations as a single extra attachment.
            // This is useful for clients that surface the annotations differently in the UI
            // but still want all the annotations to be easily downloadable in one file.
            if (snapshot.has_annotations()) {
              const auto annotations_json = ToJsonString(snapshot.annotations());
              if (annotations_json.has_value()) {
                attachments[kAttachmentAnnotations] = annotations_json.value();
              }
            }

            if (const auto integrity_report = integrity_reporter_.MakeIntegrityReport(
                    annotations_result, attachments_result,
                    datastore_->IsMissingNonPlatformAnnotations());
                integrity_report.has_value()) {
              attachments[kAttachmentManifest] = integrity_report.value();
            }

            // We bundle the attachments into a single attachment.
            if (!attachments.empty()) {
              fuchsia::feedback::Attachment bundle;
              bundle.key = kSnapshotFilename;
              if (Archive(attachments, &(bundle.value))) {
                snapshot.set_archive(std::move(bundle));
              }
            }

            return ::fit::ok(std::move(snapshot));
          })
          .then([this, callback = std::move(callback), timer_id](::fit::result<Snapshot>& result) {
            if (result.is_error()) {
              cobalt_->LogElapsedTime(cobalt::SnapshotGenerationFlow::kFailure, timer_id);
              callback(Snapshot());
            } else {
              cobalt_->LogElapsedTime(cobalt::SnapshotGenerationFlow::kSuccess, timer_id);
              callback(result.take_value());
            }
          });

  executor_.schedule_task(std::move(promise));
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
