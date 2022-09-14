// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_collector.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace crash_reports {

SnapshotCollector::SnapshotCollector(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                                     feedback_data::DataProviderInternal* data_provider,
                                     SnapshotStore* snapshot_store,
                                     zx::duration shared_request_window)
    : dispatcher_(dispatcher),
      clock_(clock),
      data_provider_(data_provider),
      snapshot_store_(snapshot_store),
      shared_request_window_(shared_request_window) {}

::fpromise::promise<SnapshotUuid> SnapshotCollector::GetSnapshotUuid(zx::duration timeout,
                                                                     ReportId report_id) {
  const zx::time current_time{clock_->Now()};

  SnapshotUuid uuid;

  if (UseLatestRequest()) {
    uuid = snapshot_requests_.back()->uuid;
  } else {
    uuid = MakeNewSnapshotRequest(current_time, timeout);
  }

  snapshot_store_->IncrementClientCount(uuid);

  const zx::time deadline = current_time + timeout;

  auto request = FindSnapshotRequest(uuid);
  FX_CHECK(request);
  request->promise_ids.insert(report_id);

  // Even though we already know the eventual snapshot uuid, we will wait to set the value in
  // |report_request_results_| until after the snapshot request is complete.
  report_results_[report_id] = std::nullopt;

  // The snapshot for |uuid| may not be ready, so the logic for returning |uuid| to the client
  // needs to be wrapped in an asynchronous task that can be re-executed when the conditions for
  // returning a UUID are met, e.g., the snapshot for |uuid| is received from |data_provider_| or
  // the call to GetSnapshotUuid times out.
  return ::fpromise::make_promise([this, uuid, deadline, report_id](::fpromise::context& context)
                                      -> ::fpromise::result<SnapshotUuid> {
    auto erase_request_task = fit::defer([this, report_id] { report_results_.erase(report_id); });

    if (shutdown_) {
      return ::fpromise::ok(kShutdownSnapshotUuid);
    }

    // The snapshot data was deleted before the promise executed. This should only occur if a
    // snapshot is dropped immediately after it is received because its annotations and archive
    // are too large and it is one of the oldest in the FIFO.
    if (!snapshot_store_->SnapshotExists(uuid)) {
      return ::fpromise::ok(kGarbageCollectedSnapshotUuid);
    }

    if (report_results_[report_id].has_value()) {
      return ::fpromise::ok(report_results_[report_id].value());
    }

    if (clock_->Now() >= deadline) {
      return ::fpromise::ok(kTimedOutSnapshotUuid);
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
          EnforceSizeLimits();
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

  snapshot_store_->AddSnapshotData(uuid, std::move(annotations), std::move(archive));

  // The snapshot request is completed and unblock all promises that need |annotations| and
  // |archive|.
  for (auto id : request->promise_ids) {
    report_results_[id] = uuid;
  }

  snapshot_requests_.erase(std::remove_if(
      snapshot_requests_.begin(), snapshot_requests_.end(),
      [uuid](const std::unique_ptr<SnapshotRequest>& request) { return uuid == request->uuid; }));
}

void SnapshotCollector::EnforceSizeLimits() {
  std::vector<std::unique_ptr<SnapshotRequest>> surviving_requests;
  for (auto& request : snapshot_requests_) {
    // If the size limits aren't exceeded, keep the request.
    if (!snapshot_store_->SizeLimitsExceeded()) {
      surviving_requests.push_back(std::move(request));

      // Continue in order to keep the rest of the requests alive.
      continue;
    }

    // Tell SnapshotStore to free space if needed. Keep the request if at least part of the snapshot
    // data survives the garbage collection.
    snapshot_store_->EnforceSizeLimits(request->uuid);
    if (snapshot_store_->SnapshotExists(request->uuid)) {
      surviving_requests.push_back(std::move(request));
    }
  }

  snapshot_requests_.swap(surviving_requests);
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
