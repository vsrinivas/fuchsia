// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace crash_reports {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::GetSnapshotParameters;
using FidlSnapshot = fuchsia::feedback::Snapshot;

template <typename V>
void AddAnnotation(const std::string& key, const V& value, FidlSnapshot* snapshot) {
  snapshot->mutable_annotations()->push_back(Annotation{
      .key = key,
      .value = std::to_string(value),
  });
}

template <>
void AddAnnotation<std::string>(const std::string& key, const std::string& value,
                                FidlSnapshot* snapshot) {
  snapshot->mutable_annotations()->push_back(Annotation{
      .key = key,
      .value = value,
  });
}

Snapshot::Annotations ToSnapshotAnnotations(const std::vector<Annotation>& annotations) {
  Snapshot::Annotations snapshot_annotations;
  for (const auto& [k, v] : annotations) {
    snapshot_annotations.emplace(k, v);
  }

  return snapshot_annotations;
}

// Helper function to make a shared_ptr from a rvalue-reference of a type.
template <typename T>
std::shared_ptr<T> MakeShared(T&& t) {
  return std::make_shared<T>(static_cast<std::remove_reference_t<T>&&>(t));
}

}  // namespace

SnapshotManager::SnapshotManager(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 std::unique_ptr<timekeeper::Clock> clock,
                                 zx::duration shared_request_window,
                                 StorageSize max_annotations_size, StorageSize max_archives_size)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      clock_(std::move(clock)),
      shared_request_window_(shared_request_window),
      max_annotations_size_(max_annotations_size),
      current_annotations_size_(0u),
      max_archives_size_(max_archives_size),
      current_archives_size_(0u),
      garbage_collected_snapshot_("garbage collected"),
      timed_out_snapshot_("timed out"),
      no_uuid_snapshot_(UuidForNoSnapshotUuid()) {
  garbage_collected_snapshot_.annotations->emplace("debug.snapshot.error", "garbage collected");
  garbage_collected_snapshot_.annotations->emplace("debug.snapshot.present", "false");

  timed_out_snapshot_.annotations->emplace("debug.snapshot.error", "timeout");
  timed_out_snapshot_.annotations->emplace("debug.snapshot.present", "false");

  no_uuid_snapshot_.annotations->emplace("debug.snapshot.error", "missing uuid");
  no_uuid_snapshot_.annotations->emplace("debug.snapshot.present", "false");
}

void SnapshotManager::Connect() {
  if (data_provider_.is_bound()) {
    return;
  }

  data_provider_ = services_->Connect<fuchsia::feedback::DataProvider>();

  // On disconnect, complete all pending requests with a default snapshot.
  data_provider_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchisa.feedback.DataProvider";

    for (auto& request : requests_) {
      if (!request.is_pending) {
        continue;
      }

      FidlSnapshot snapshot;
      AddAnnotation("debug.snapshot.error", ToReason(Error::kConnectionError), &snapshot);
      CompleteWithSnapshot(request.uuid, std::move(snapshot));
    }

    // Call EnforceSizeLimits after the for-loop to not invalidate |request_| while it's being
    // iterated over.
    EnforceSizeLimits();
  });
}

Snapshot SnapshotManager::GetSnapshot(const SnapshotUuid& uuid) {
  if (uuid == garbage_collected_snapshot_.uuid) {
    return Snapshot(garbage_collected_snapshot_.annotations);
  }

  if (uuid == timed_out_snapshot_.uuid) {
    return Snapshot(timed_out_snapshot_.annotations);
  }

  if (uuid == no_uuid_snapshot_.uuid) {
    return Snapshot(no_uuid_snapshot_.annotations);
  }

  auto* data = FindSnapshotData(uuid);

  if (!data) {
    return Snapshot(garbage_collected_snapshot_.annotations);
  }

  return Snapshot(data->annotations, data->archive);
}

::fit::promise<SnapshotUuid> SnapshotManager::GetSnapshotUuid(zx::duration timeout) {
  const zx::time current_time{clock_->Now()};

  SnapshotUuid uuid;

  if (UseLatestRequest(current_time)) {
    uuid = requests_.back().uuid;
  } else {
    uuid = MakeNewSnapshotRequest(current_time, timeout);
  }

  auto* data = FindSnapshotData(uuid);
  FX_CHECK(data);

  data->num_clients_with_uuid += 1;

  const zx::time deadline = current_time + timeout;

  // The snapshot for |uuid| may not be ready, so the logic for returning |uuid| to the client
  // needs to be wrapped in an asynchronous task that can be re-executed when the conditions for
  // returning a UUID are met, e.g., the snapshot for |uuid| is received from |data_provider_| or
  // the call to GetSnapshotUuid times out.
  return ::fit::make_promise(
      [this, uuid, deadline](::fit::context& context) -> ::fit::result<SnapshotUuid> {
        auto request = FindSnapshotRequest(uuid);

        // The request and its data were deleted before the promise executed. This should only occur
        // if a snapshot is dropped immediately after it is received because its annotations and
        // archive are too large and it is one of the oldest in the FIFO.
        if (!request) {
          return ::fit::ok(garbage_collected_snapshot_.uuid);
        }

        if (!request->is_pending) {
          return ::fit::ok(request->uuid);
        }

        if (clock_->Now() >= deadline) {
          return ::fit::ok(timed_out_snapshot_.uuid);
        }

        WaitForSnapshot(uuid, deadline, context.suspend_task());
        return ::fit::pending();
      });
}

void SnapshotManager::Release(const SnapshotUuid& uuid) {
  if (uuid == garbage_collected_snapshot_.uuid || uuid == timed_out_snapshot_.uuid ||
      uuid == no_uuid_snapshot_.uuid) {
    return;
  }

  auto* data = FindSnapshotData(uuid);

  // The snapshot was likely dropped due to size constraints.
  if (!data) {
    return;
  }

  data->num_clients_with_uuid -= 1;

  // There are still clients that need the snapshot.
  if (data->num_clients_with_uuid > 0) {
    return;
  }

  DropAnnotations(data);
  DropArchive(data);

  // No calls to GetUuid should be blocked.
  if (auto request = FindSnapshotRequest(uuid); request) {
    FX_CHECK(request->blocked_promises.empty());
  }

  requests_.erase(
      std::remove_if(requests_.begin(), requests_.end(),
                     [uuid](const SnapshotRequest& request) { return uuid == request.uuid; }));
  data_.erase(uuid);
}

SnapshotUuid SnapshotManager::MakeNewSnapshotRequest(const zx::time start_time,
                                                     const zx::duration timeout) {
  const auto uuid = uuid::Generate();
  requests_.emplace_back(SnapshotRequest{
      .uuid = uuid,
      .start_time = start_time,
      .is_pending = true,
      .blocked_promises = {},
  });
  data_.emplace(uuid, SnapshotData{
                          .num_clients_with_uuid = 0,
                          .annotations_size = StorageSize::Bytes(0u),
                          .archive_size = StorageSize::Bytes(0u),
                          .annotations = nullptr,
                          .archive = nullptr,
                      });

  if (!data_provider_.is_bound()) {
    Connect();
  }

  GetSnapshotParameters params;

  // Give 15s for the packaging of the snapshot and the round-trip between the client and
  // the server and the rest is given to each data collection.
  params.set_collection_timeout_per_data((timeout - zx::sec(15)).get());

  data_provider_->GetSnapshot(std::move(params), [this, uuid](FidlSnapshot snapshot) {
    CompleteWithSnapshot(uuid, std::move(snapshot));
    EnforceSizeLimits();
  });

  return uuid;
}

void SnapshotManager::WaitForSnapshot(const SnapshotUuid& uuid, zx::time deadline,
                                      ::fit::suspended_task get_uuid_promise) {
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

void SnapshotManager::CompleteWithSnapshot(const SnapshotUuid& uuid, FidlSnapshot fidl_snapshot) {
  auto* request = FindSnapshotRequest(uuid);
  auto* data = FindSnapshotData(uuid);

  // A pending request shouldn't be deleted.
  FX_CHECK(request);
  FX_CHECK(data);
  FX_CHECK(request->is_pending);

  // Add debug annotations.
  if (fidl_snapshot.IsEmpty()) {
    AddAnnotation("debug.snapshot.present", std::string("false"), &fidl_snapshot);
  }
  AddAnnotation("debug.snapshot.shared-request.num-clients", data->num_clients_with_uuid,
                &fidl_snapshot);
  AddAnnotation("debug.snapshot.shared-request.uuid", request->uuid, &fidl_snapshot);

  // Take ownership of |fidl_snapshot| and the record the size of its annotations and archive.
  if (fidl_snapshot.has_annotations()) {
    data->annotations = MakeShared(ToSnapshotAnnotations(fidl_snapshot.annotations()));

    for (const auto& [k, v] : *(data->annotations)) {
      data->annotations_size += StorageSize::Bytes(k.size());
      data->annotations_size += StorageSize::Bytes(v.size());
    }
    current_annotations_size_ += data->annotations_size;
  }

  if (fidl_snapshot.has_archive()) {
    data->archive = MakeShared(Snapshot::Archive(fidl_snapshot.archive()));

    data->archive_size += StorageSize::Bytes(data->archive->key.size());
    data->archive_size += StorageSize::Bytes(data->archive->value.size());
    current_archives_size_ += data->archive_size;
  }

  // The request is completed and unblock all promises that need |snapshot|.
  request->is_pending = false;
  for (auto& blocked_promise : request->blocked_promises) {
    if (blocked_promise) {
      blocked_promise.resume_task();
    }
  }
  request->blocked_promises.clear();
}

void SnapshotManager::EnforceSizeLimits() {
  std::vector<SnapshotRequest> surviving_requests;
  for (auto& request : requests_) {
    // If the request is pending or the size limits aren't exceeded, keep the request.
    if (request.is_pending || (current_annotations_size_ <= max_annotations_size_ &&
                               current_archives_size_ <= max_archives_size_)) {
      surviving_requests.push_back(std::move(request));

      // Continue in order to keep the rest of the requests alive.
      continue;
    }

    auto* data = FindSnapshotData(request.uuid);
    FX_CHECK(data);

    // Drop |request|'s annotations if necessary.
    if (current_annotations_size_ > max_annotations_size_) {
      DropAnnotations(data);
    }

    // Drop |request|'s archive if necessary.
    if (current_archives_size_ > max_archives_size_) {
      DropArchive(data);
    }

    // Delete the SnapshotRequest and SnapshotData if the annotations and archive have been
    // dropped, either in this iteration of the loop or a prior one.
    if (!data->annotations && !data->archive) {
      data_.erase(request.uuid);
      continue;
    }

    surviving_requests.push_back(std::move(request));
  }

  requests_.swap(surviving_requests);
}

void SnapshotManager::DropAnnotations(SnapshotData* data) {
  data->annotations = nullptr;

  current_annotations_size_ -= data->annotations_size;
  data->annotations_size = StorageSize::Bytes(0u);
}

void SnapshotManager::DropArchive(SnapshotData* data) {
  data->archive = nullptr;

  current_archives_size_ -= data->archive_size;
  data->archive_size = StorageSize::Bytes(0u);
}

bool SnapshotManager::UseLatestRequest(const zx::time time) const {
  if (requests_.empty()) {
    return false;
  }

  return (time >= requests_.back().start_time) &&
         (time < requests_.back().start_time + shared_request_window_);
}

SnapshotManager::SnapshotRequest* SnapshotManager::FindSnapshotRequest(const SnapshotUuid& uuid) {
  auto request =
      std::find_if(requests_.begin(), requests_.end(),
                   [uuid](const SnapshotRequest& request) { return uuid == request.uuid; });
  return (request == requests_.end()) ? nullptr : &(*request);
}

SnapshotManager::SnapshotData* SnapshotManager::FindSnapshotData(const SnapshotUuid& uuid) {
  return (data_.find(uuid) == data_.end()) ? nullptr : &(data_.at(uuid));
}

}  // namespace crash_reports
}  // namespace forensics
