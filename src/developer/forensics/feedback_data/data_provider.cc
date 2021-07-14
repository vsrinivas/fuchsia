// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
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
#include "src/lib/fsl/vmo/sized_vmo.h"
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
                           timekeeper::Clock* clock, const bool is_first_instance,
                           const AnnotationKeys& annotation_allowlist,
                           const AttachmentKeys& attachment_allowlist, cobalt::Logger* cobalt,
                           Datastore* datastore, InspectDataBudget* inspect_data_budget)
    : dispatcher_(dispatcher),
      services_(services),
      metadata_(dispatcher_, clock, is_first_instance, annotation_allowlist, attachment_allowlist),
      cobalt_(cobalt),
      datastore_(datastore),
      executor_(dispatcher_),
      inspect_data_budget_(inspect_data_budget) {}

void DataProvider::GetAnnotations(fuchsia::feedback::GetAnnotationsParameters params,
                                  GetAnnotationsCallback callback) {
  const zx::duration timeout = (params.has_collection_timeout_per_annotation())
                                   ? zx::duration(params.collection_timeout_per_annotation())
                                   : kDefaultDataTimeout;

  auto promise = datastore_->GetAnnotations(timeout).and_then(
      [callback = std::move(callback)](Annotations& annotations) {
        callback(std::move(fuchsia::feedback::Annotations().set_annotations(
            ToFeedbackAnnotationVector(annotations))));
      });

  // TODO(fxbug.dev/74102): Track how long GetAnnotations took via Cobalt.

  executor_.schedule_task(std::move(promise));
}

void DataProvider::GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                               GetSnapshotCallback callback) {
  const zx::duration timeout = (params.has_collection_timeout_per_data())
                                   ? zx::duration(params.collection_timeout_per_data())
                                   : kDefaultDataTimeout;

  std::optional<zx::channel> channel;
  if (params.has_response_channel()) {
    channel = std::move(*params.mutable_response_channel());
  }

  const uint64_t timer_id = cobalt_->StartTimer();
  auto promise =
      ::fpromise::join_promises(datastore_->GetAnnotations(timeout),
                                datastore_->GetAttachments(timeout))
          .and_then(
              [this, channel = std::move(channel)](
                  std::tuple<::fpromise::result<Annotations>, ::fpromise::result<Attachments>>&
                      annotations_and_attachments) mutable {
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

                attachments[kAttachmentMetadata] =
                    metadata_.MakeMetadata(annotations_result, attachments_result,
                                           datastore_->IsMissingNonPlatformAnnotations());

                // We bundle the attachments into a single archive.
                if (!attachments.empty()) {
                  fsl::SizedVmo archive;
                  std::map<std::string, ArchiveFileStats> file_size_stats;
                  if (Archive(attachments, &archive, &file_size_stats)) {
                    inspect_data_budget_->UpdateBudget(file_size_stats);
                    cobalt_->LogCount(SnapshotVersion::kCobalt, (uint64_t)archive.size());
                    if (channel) {
                      ServeArchive(std::move(archive), std::move(channel.value()));
                    } else {
                      snapshot.set_archive(
                          {.key = kSnapshotFilename, .value = std::move(archive).ToTransport()});
                    }
                  }
                }

                return ::fpromise::ok(std::move(snapshot));
              })
          .then([this, callback = std::move(callback),
                 timer_id](::fpromise::result<Snapshot>& result) {
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

bool DataProvider::ServeArchive(fsl::SizedVmo archive, zx::channel server_end) {
  const size_t archive_index = next_served_archive_index_++;
  auto served_archive = std::make_unique<ServedArchive>(std::move(archive));

  if (!served_archive->Serve(std::move(server_end), dispatcher_, [this, archive_index]() mutable {
        served_archives_.erase(archive_index);
      })) {
    return false;
  }

  served_archives_.emplace(archive_index, std::move(served_archive));
  return true;
}

ServedArchive::ServedArchive(fsl::SizedVmo archive)
    : file_(std::move(archive.vmo()), 0, archive.size()) {}

bool ServedArchive::Serve(zx::channel server_end, async_dispatcher_t* dispatcher,
                          std::function<void()> completed) {
  channel_closed_observer_ =
      std::make_unique<async::WaitOnce>(server_end.get(), ZX_CHANNEL_PEER_CLOSED);
  if (const auto status = file_.Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(server_end));
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Cannot serve archive";
    return false;
  }

  if (const auto status = channel_closed_observer_->Begin(
          dispatcher, [completed = std::move(completed)](...) { completed(); });
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Cannot attach observer to server end";
    return false;
  }

  return true;
}

void DataProvider::GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) {
  auto promise =
      TakeScreenshot(
          dispatcher_, services_,
          fit::Timeout(kScreenshotTimeout,
                       [this] { cobalt_->LogOccurrence(cobalt::TimedOutData::kScreenshot); }))
          .and_then([encoding](fuchsia::ui::scenic::ScreenshotData& raw_screenshot)
                        -> ::fpromise::result<Screenshot> {
            Screenshot screenshot;
            screenshot.dimensions_in_px.height = raw_screenshot.info.height;
            screenshot.dimensions_in_px.width = raw_screenshot.info.width;
            switch (encoding) {
              case ImageEncoding::PNG:
                if (!RawToPng(raw_screenshot.data, raw_screenshot.info.height,
                              raw_screenshot.info.width, raw_screenshot.info.stride,
                              raw_screenshot.info.pixel_format, &screenshot.image)) {
                  FX_LOGS(ERROR) << "Failed to convert raw screenshot to PNG";
                  return ::fpromise::error();
                }
                break;
            }
            return ::fpromise::ok(std::move(screenshot));
          })
          .then([callback = std::move(callback)](::fpromise::result<Screenshot>& result) {
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
