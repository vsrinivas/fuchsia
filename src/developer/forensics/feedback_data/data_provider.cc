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

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/encode.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/image_conversion.h"
#include "src/developer/forensics/feedback_data/screenshot.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/uuid/uuid.h"

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
                           timekeeper::Clock* clock, RedactorBase* redactor,
                           const bool is_first_instance,
                           const std::set<std::string>& annotation_allowlist,
                           const feedback::AttachmentKeys& attachment_allowlist,
                           cobalt::Logger* cobalt, feedback::AnnotationManager* annotation_manager,
                           feedback::AttachmentManager* attachment_manager,
                           InspectDataBudget* inspect_data_budget)
    : dispatcher_(dispatcher),
      services_(services),
      metadata_(dispatcher_, clock, redactor, is_first_instance, annotation_allowlist,
                attachment_allowlist),
      cobalt_(cobalt),
      annotation_manager_(annotation_manager),
      annotation_metrics_(cobalt_),
      attachment_manager_(attachment_manager),
      attachment_metrics_(cobalt_),
      executor_(dispatcher_),
      inspect_data_budget_(inspect_data_budget) {}

::fpromise::promise<feedback::Annotations> DataProvider::GetAnnotations(
    const zx::duration timeout) {
  return annotation_manager_->GetAll(timeout).and_then([this](feedback::Annotations& annotations) {
    annotation_metrics_.LogMetrics(annotations);
    return ::fpromise::ok(std::move(annotations));
  });
}

::fpromise::promise<feedback::Attachments> DataProvider::GetAttachments(
    const zx::duration timeout) {
  return attachment_manager_->GetAttachments(timeout).and_then(
      [this](feedback::Attachments& attachments) {
        attachment_metrics_.LogMetrics(attachments);
        return ::fpromise::ok(std::move(attachments));
      });
}

void DataProvider::GetAnnotations(fuchsia::feedback::GetAnnotationsParameters params,
                                  GetAnnotationsCallback callback) {
  const auto timeout = (params.has_collection_timeout_per_annotation())
                           ? zx::duration(params.collection_timeout_per_annotation())
                           : kDefaultDataTimeout;

  // TODO(fxbug.dev/74102): Track how long GetAnnotations took via Cobalt.
  executor_.schedule_task(GetAnnotations(timeout).and_then(
      [callback = std::move(callback)](feedback::Annotations& annotations) {
        callback(feedback::Encode<fuchsia::feedback::Annotations>(annotations));
      }));
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

  GetSnapshotInternal(timeout, [this, callback = std::move(callback), channel = std::move(channel)](
                                   const feedback::Annotations& annotations,
                                   fsl::SizedVmo archive) mutable {
    Snapshot snapshot;

    // Add the annotations to the FIDL object and as file in the snapshot itself.
    if (auto fidl = feedback::Encode<fuchsia::feedback::Annotations>(annotations);
        fidl.has_annotations()) {
      snapshot.set_annotations(fidl.annotations());
    }

    if (archive.vmo().is_valid()) {
      if (channel) {
        ServeArchive(std::move(archive), std::move(channel.value()));
      } else {
        snapshot.set_archive({.key = kSnapshotFilename, .value = std::move(archive).ToTransport()});
      }
    }

    callback(std::move(snapshot));
  });
}

void DataProvider::GetSnapshotInternal(zx::duration timeout, GetSnapshotInternalCallback callback) {
  GetSnapshotInternal(timeout, [callback = std::move(callback)](
                                   const feedback::Annotations& annotations,
                                   fsl::SizedVmo archive) mutable {
    callback(annotations, {.key = kSnapshotFilename, .value = std::move(archive).ToTransport()});
  });
}

void DataProvider::GetSnapshotInternal(
    zx::duration timeout, fit::callback<void(feedback::Annotations, fsl::SizedVmo)> callback) {
  const uint64_t timer_id = cobalt_->StartTimer();

  auto join = ::fpromise::join_promises(GetAnnotations(timeout), GetAttachments(timeout));
  using result_t = decltype(join)::value_type;

  auto promise =
      join.and_then([this, timer_id, callback = std::move(callback)](result_t& results) mutable {
        FX_CHECK(std::get<0>(results).is_ok()) << "Impossible annotation collection failure";
        FX_CHECK(std::get<1>(results).is_ok()) << "Impossible attachment collection failure";

        const auto& annotations = std::get<0>(results).value();
        const auto& attachments = std::get<1>(results).value();
        std::map<std::string, std::string> snapshot_files;

        // Add the annotations to |snapshot_files|
        auto file = feedback::Encode<std::string>(annotations);
        snapshot_files[kAttachmentAnnotations] = std::move(file);

        // Add the attachments to |snapshot_files|
        for (const auto& [key, value] : attachments) {
          if (value.HasValue()) {
            snapshot_files[key] = value.Value();
          }
        }

        snapshot_files[kAttachmentMetadata] =
            metadata_.MakeMetadata(annotations, attachments, uuid::Generate(),
                                   annotation_manager_->IsMissingNonPlatformAnnotations());

        fsl::SizedVmo archive;

        // We bundle the attachments into a single archive.
        if (std::map<std::string, ArchiveFileStats> file_size_stats;
            Archive(snapshot_files, &archive, &file_size_stats)) {
          inspect_data_budget_->UpdateBudget(file_size_stats);
          cobalt_->LogCount(SnapshotVersion::kCobalt, archive.size());
          cobalt_->LogElapsedTime(cobalt::SnapshotGenerationFlow::kSuccess, timer_id);
        } else {
          cobalt_->LogElapsedTime(cobalt::SnapshotGenerationFlow::kFailure, timer_id);
          archive.vmo().reset();
        }
        callback(annotations, std::move(archive));
        return ::fpromise::ok();
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
  if (const auto status =
          file_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE, std::move(server_end));
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
      TakeScreenshot(dispatcher_, services_, kScreenshotTimeout)
          .or_else([this](const Error& error) {
            if (error != Error::kTimeout) {
              return ::fpromise::error();
            }

            cobalt_->LogOccurrence(cobalt::TimedOutData::kScreenshot);
            return ::fpromise::error();
          })
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
