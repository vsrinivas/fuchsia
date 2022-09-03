// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace crash_reports {

SnapshotManager::SnapshotManager(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                                 feedback_data::DataProviderInternal* data_provider,
                                 feedback::AnnotationManager* annotation_manager,
                                 zx::duration shared_request_window,
                                 const std::string& garbage_collected_snapshots_path,
                                 StorageSize max_annotations_size, StorageSize max_archives_size)
    : dispatcher_(dispatcher),
      clock_(clock),
      data_provider_(data_provider),
      shared_request_window_(shared_request_window),
      snapshot_store_(annotation_manager, garbage_collected_snapshots_path, max_annotations_size,
                      max_archives_size) {}

Snapshot SnapshotManager::GetSnapshot(const SnapshotUuid& uuid) {
  return snapshot_store_.GetSnapshot(uuid);
}

::fpromise::promise<SnapshotUuid> SnapshotManager::GetSnapshotUuid(zx::duration timeout) {
  const zx::time current_time{clock_->Now()};

  SnapshotUuid uuid;

  if (UseLatestRequest()) {
    uuid = requests_.back()->uuid;
  } else {
    uuid = MakeNewSnapshotRequest(current_time, timeout);
  }

  snapshot_store_.IncrementClientCount(uuid);

  const zx::time deadline = current_time + timeout;

  // The snapshot for |uuid| may not be ready, so the logic for returning |uuid| to the client
  // needs to be wrapped in an asynchronous task that can be re-executed when the conditions for
  // returning a UUID are met, e.g., the snapshot for |uuid| is received from |data_provider_| or
  // the call to GetSnapshotUuid times out.
  return ::fpromise::make_promise(
      [this, uuid, deadline](::fpromise::context& context) -> ::fpromise::result<SnapshotUuid> {
        if (shutdown_) {
          return ::fpromise::ok(kShutdownSnapshotUuid);
        }

        auto request = FindSnapshotRequest(uuid);

        // The request and its data were deleted before the promise executed. This should only occur
        // if a snapshot is dropped immediately after it is received because its annotations and
        // archive are too large and it is one of the oldest in the FIFO.
        if (!request) {
          return ::fpromise::ok(kGarbageCollectedSnapshotUuid);
        }

        if (!request->is_pending) {
          return ::fpromise::ok(request->uuid);
        }

        if (clock_->Now() >= deadline) {
          return ::fpromise::ok(kTimedOutSnapshotUuid);
        }

        WaitForSnapshot(uuid, deadline, context.suspend_task());
        return ::fpromise::pending();
      });
}

void SnapshotManager::Release(const SnapshotUuid& uuid) {
  if (const bool garbage_collected = snapshot_store_.Release(uuid); garbage_collected) {
    // No calls to GetUuid should be blocked.
    if (auto request = FindSnapshotRequest(uuid); request) {
      FX_CHECK(request->blocked_promises.empty());
    }

    requests_.erase(std::remove_if(
        requests_.begin(), requests_.end(),
        [uuid](const std::unique_ptr<SnapshotRequest>& request) { return uuid == request->uuid; }));
  }
}

void SnapshotManager::Shutdown() {
  // Unblock all pending promises to return |shutdown_snapshot_|.
  shutdown_ = true;
  for (auto& request : requests_) {
    if (!request->is_pending) {
      continue;
    }

    for (auto& blocked_promise : request->blocked_promises) {
      if (blocked_promise) {
        blocked_promise.resume_task();
      }
    }
    request->blocked_promises.clear();
  }
}

SnapshotUuid SnapshotManager::MakeNewSnapshotRequest(const zx::time start_time,
                                                     const zx::duration timeout) {
  const auto uuid = uuid::Generate();
  requests_.emplace_back(std::unique_ptr<SnapshotRequest>(new SnapshotRequest{
      .uuid = uuid,
      .is_pending = true,
      .blocked_promises = {},
      .delayed_get_snapshot = async::TaskClosure(),
  }));

  snapshot_store_.StartSnapshot(uuid);

  requests_.back()->delayed_get_snapshot.set_handler([this, timeout, uuid]() {
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
  requests_.back()->delayed_get_snapshot.PostForTime(dispatcher_,
                                                     start_time + shared_request_window_);

  return uuid;
}

void SnapshotManager::WaitForSnapshot(const SnapshotUuid& uuid, zx::time deadline,
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
            if (auto* request = FindSnapshotRequest(uuid); request && request->is_pending) {
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

void SnapshotManager::CompleteWithSnapshot(const SnapshotUuid& uuid,
                                           feedback::Annotations annotations,
                                           fuchsia::feedback::Attachment archive) {
  auto* request = FindSnapshotRequest(uuid);

  // A pending request shouldn't be deleted.
  FX_CHECK(request);
  FX_CHECK(request->is_pending);

  snapshot_store_.AddSnapshotData(uuid, std::move(annotations), std::move(archive));

  // The request is completed and unblock all promises that need |annotations| and |archive|.
  request->is_pending = false;
  for (auto& blocked_promise : request->blocked_promises) {
    if (blocked_promise) {
      blocked_promise.resume_task();
    }
  }
  request->blocked_promises.clear();
}

void SnapshotManager::EnforceSizeLimits() {
  std::vector<std::unique_ptr<SnapshotRequest>> surviving_requests;
  for (auto& request : requests_) {
    // If the request is pending or the size limits aren't exceeded, keep the request.
    if (request->is_pending || !snapshot_store_.SizeLimitsExceeded()) {
      surviving_requests.push_back(std::move(request));

      // Continue in order to keep the rest of the requests alive.
      continue;
    }

    // Tell SnapshotStore to free space if needed. Keep the request if at least part of the snapshot
    // data survives the garbage collection.
    snapshot_store_.EnforceSizeLimits(request->uuid);
    if (snapshot_store_.SnapshotExists(request->uuid)) {
      surviving_requests.push_back(std::move(request));
    }
  }

  requests_.swap(surviving_requests);
}

bool SnapshotManager::UseLatestRequest() const {
  if (requests_.empty()) {
    return false;
  }

  // Whether the FIDL call for the latest request has already been made or not. If it has, the
  // snapshot might not contain all the logs up until now for instance so it's better to create a
  // new request.
  return requests_.back()->delayed_get_snapshot.is_pending();
}

SnapshotManager::SnapshotRequest* SnapshotManager::FindSnapshotRequest(const SnapshotUuid& uuid) {
  auto request = std::find_if(
      requests_.begin(), requests_.end(),
      [uuid](const std::unique_ptr<SnapshotRequest>& request) { return uuid == request->uuid; });
  return (request == requests_.end()) ? nullptr : request->get();
}

}  // namespace crash_reports
}  // namespace forensics
