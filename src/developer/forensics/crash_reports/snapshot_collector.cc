// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_collector.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/report_id.h"
#include "src/developer/forensics/crash_reports/report_util.h"
#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace crash_reports {

namespace {

template <typename V>
void AddAnnotation(const std::string& key, const V& value, feedback::Annotations& annotations) {
  annotations.insert({key, std::to_string(value)});
}

template <>
void AddAnnotation<std::string>(const std::string& key, const std::string& value,
                                feedback::Annotations& annotations) {
  annotations.insert({key, value});
}

}  // namespace

SnapshotCollector::SnapshotCollector(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                                     feedback_data::DataProviderInternal* data_provider,
                                     SnapshotStore* snapshot_store,
                                     zx::duration shared_request_window)
    : dispatcher_(dispatcher),
      clock_(clock),
      data_provider_(data_provider),
      snapshot_store_(snapshot_store),
      shared_request_window_(shared_request_window) {}

feedback::Annotations SnapshotCollector::GetMissingSnapshotAnnotations(const SnapshotUuid& uuid) {
  const auto missing_snapshot = snapshot_store_->GetMissingSnapshot(uuid);

  feedback::Annotations combined_annotations = missing_snapshot.Annotations();
  for (const auto& [key, val] : missing_snapshot.PresenceAnnotations()) {
    combined_annotations.insert_or_assign(key, val);
  }

  return combined_annotations;
}

::fpromise::promise<Report> SnapshotCollector::GetReport(
    const zx::duration timeout, fuchsia::feedback::CrashReport fidl_report,
    const ReportId report_id, const std::optional<timekeeper::time_utc> current_utc_time,
    const Product& product, const bool is_hourly_snapshot, const ReportingPolicy reporting_policy) {
  auto GetReport =
      [fidl_report = std::move(fidl_report), report_id, current_utc_time, product,
       is_hourly_snapshot](
          const SnapshotUuid& uuid,
          const feedback::Annotations& annotations) mutable -> ::fpromise::result<Report> {
    return MakeReport(std::move(fidl_report), report_id, uuid, annotations, current_utc_time,
                      product, is_hourly_snapshot);
  };

  // Only generate a snapshot if the report won't be immediately archived in the filesystem in
  // order to save time during crash report creation.
  if (reporting_policy == ReportingPolicy::kArchive) {
    return fpromise::make_result_promise(
        GetReport(kNoUuidSnapshotUuid, GetMissingSnapshotAnnotations(kNoUuidSnapshotUuid)));
  }

  const zx::time current_time{clock_->Now()};

  SnapshotUuid uuid;

  if (UseLatestRequest()) {
    uuid = snapshot_requests_.back()->uuid;
  } else {
    uuid = MakeNewSnapshotRequest(current_time, timeout);
  }

  snapshot_store_->IncrementClientCount(uuid);

  const zx::time deadline = current_time + timeout;

  auto* request = FindSnapshotRequest(uuid);
  FX_CHECK(request);
  request->promise_ids.insert(report_id);

  // Even though we already know the eventual snapshot uuid, we will wait to set the value in
  // |report_results_| until after the snapshot request is complete.
  report_results_[report_id] = std::nullopt;

  // The snapshot for |uuid| may not be ready, so the logic for returning |uuid| to the client
  // needs to be wrapped in an asynchronous task that can be re-executed when the conditions for
  // returning a UUID are met, e.g., the snapshot for |uuid| is received from |data_provider_| or
  // the call to GetSnapshotUuid times out.
  return ::fpromise::make_promise(
      [this, uuid, deadline, report_id, GetReport = std::move(GetReport)](
          ::fpromise::context& context) mutable -> ::fpromise::result<Report> {
        auto erase_request_task =
            fit::defer([this, report_id] { report_results_.erase(report_id); });

        if (shutdown_) {
          return GetReport(kShutdownSnapshotUuid,
                           GetMissingSnapshotAnnotations(kShutdownSnapshotUuid));
        }

        // The snapshot data was deleted before the promise executed. This should only occur if a
        // snapshot is dropped immediately after it is received because its annotations and archive
        // are too large and it is one of the oldest in the FIFO.
        if (!snapshot_store_->SnapshotExists(uuid)) {
          return GetReport(kGarbageCollectedSnapshotUuid,
                           GetMissingSnapshotAnnotations(kGarbageCollectedSnapshotUuid));
        }

        if (report_results_[report_id].has_value()) {
          return GetReport(report_results_[report_id].value().uuid,
                           *report_results_[report_id].value().annotations);
        }

        if (clock_->Now() >= deadline) {
          return GetReport(kTimedOutSnapshotUuid,
                           GetMissingSnapshotAnnotations(kTimedOutSnapshotUuid));
        }

        WaitForSnapshot(uuid, deadline, context.suspend_task());
        erase_request_task.cancel();
        return ::fpromise::pending();
      });
}

void SnapshotCollector::Shutdown() {
  // The destructor of snapshot requests will unblock all pending promises to return
  // |shutdown_snapshot_|.
  shutdown_ = true;
  snapshot_requests_.clear();
}

SnapshotUuid SnapshotCollector::MakeNewSnapshotRequest(const zx::time start_time,
                                                       const zx::duration timeout) {
  const auto uuid = uuid::Generate();
  snapshot_requests_.emplace_back(std::unique_ptr<SnapshotRequest>(new SnapshotRequest{
      .uuid = uuid,
      .promise_ids = {},
      .blocked_promises = {},
      .delayed_get_snapshot = async::TaskClosure(),
  }));

  snapshot_store_->StartSnapshot(uuid);

  snapshot_requests_.back()->delayed_get_snapshot.set_handler([this, timeout, uuid]() {
    // Give 15s for the packaging of the snapshot and the round-trip between the client and
    // the server and the rest is given to each data collection.
    zx::duration collection_timeout_per_data = timeout - zx::sec(15);
    data_provider_->GetSnapshotInternal(
        collection_timeout_per_data,
        [this, uuid](feedback::Annotations annotations, fuchsia::feedback::Attachment archive) {
          CompleteWithSnapshot(uuid, std::move(annotations), std::move(archive));
        });
  });
  snapshot_requests_.back()->delayed_get_snapshot.PostForTime(dispatcher_,
                                                              start_time + shared_request_window_);

  return uuid;
}

void SnapshotCollector::WaitForSnapshot(const SnapshotUuid& uuid, zx::time deadline,
                                        ::fpromise::suspended_task get_uuid_promise) {
  auto* request = FindSnapshotRequest(uuid);
  if (!request) {
    get_uuid_promise.resume_task();
    return;
  }

  request->blocked_promises.push_back(std::move(get_uuid_promise));
  const size_t idx = request->blocked_promises.size() - 1;

  // Resume |get_uuid_promise| after |deadline| has passed.
  if (const zx_status_t status = async::PostTaskForTime(
          dispatcher_,
          [this, idx, uuid] {
            if (auto* request = FindSnapshotRequest(uuid); request) {
              FX_CHECK(idx < request->blocked_promises.size());
              if (request->blocked_promises[idx]) {
                request->blocked_promises[idx].resume_task();
              }
            }
          },
          deadline);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post async task";

    // Immediately resume the promise if posting the task fails.
    request->blocked_promises.back().resume_task();
    request->blocked_promises.pop_back();
  }
}

void SnapshotCollector::CompleteWithSnapshot(const SnapshotUuid& uuid,
                                             feedback::Annotations annotations,
                                             fuchsia::feedback::Attachment archive) {
  auto* request = FindSnapshotRequest(uuid);
  FX_CHECK(request);

  // Add annotations about the snapshot. These are not "presence" annotations because
  // they're unchanging and not the result of the SnapshotManager's data management.
  AddAnnotation("debug.snapshot.shared-request.num-clients", request->promise_ids.size(),
                annotations);
  AddAnnotation("debug.snapshot.shared-request.uuid", uuid, annotations);

  if (archive.key.empty() || !archive.value.vmo.is_valid()) {
    AddAnnotation("debug.snapshot.present", std::string("false"), annotations);
  }

  // The snapshot request is completed and unblock all promises that need |annotations| and
  // |archive|.
  const auto shared_annotations = std::make_shared<feedback::Annotations>(std::move(annotations));
  for (auto id : request->promise_ids) {
    report_results_[id] = ReportResults{
        .uuid = uuid,
        .annotations = shared_annotations,
    };
  }

  snapshot_requests_.erase(std::remove_if(
      snapshot_requests_.begin(), snapshot_requests_.end(),
      [uuid](const std::unique_ptr<SnapshotRequest>& request) { return uuid == request->uuid; }));

  // Now that all crash reports associated with this snapshot have extracted the necessary
  // annotations we can move the snapshot to |snapshot_store_|.
  snapshot_store_->AddSnapshot(uuid, std::move(archive));
}

bool SnapshotCollector::UseLatestRequest() const {
  if (snapshot_requests_.empty()) {
    return false;
  }

  // Whether the FIDL call for the latest request has already been made or not. If it has, the
  // snapshot might not contain all the logs up until now for instance so it's better to create a
  // new request.
  return snapshot_requests_.back()->delayed_get_snapshot.is_pending();
}

SnapshotCollector::SnapshotRequest* SnapshotCollector::FindSnapshotRequest(
    const SnapshotUuid& uuid) {
  auto request = std::find_if(
      snapshot_requests_.begin(), snapshot_requests_.end(),
      [uuid](const std::unique_ptr<SnapshotRequest>& request) { return uuid == request->uuid; });
  return (request == snapshot_requests_.end()) ? nullptr : request->get();
}

}  // namespace crash_reports
}  // namespace forensics
