// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/snapshot_request_manager.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics {
namespace feedback_data {

using fuchsia::feedback::Snapshot;
using GetSnapshotCallback = fuchsia::feedback::DataProvider::GetSnapshotCallback;

SnapshotRequestManager::SnapshotRequestManager(zx::duration delta,
                                               std::unique_ptr<timekeeper::Clock> clock)
    : delta_(delta), clock_(std::move(clock)) {}

std::optional<uint64_t> SnapshotRequestManager::Manage(zx::duration request_timeout,
                                                       GetSnapshotCallback request_callback) {
  const zx::time creation_time(clock_->Now());

  // Attempts to find an existing pool to add |callback| to.
  //
  // A valid pool is one that (1) was created less than |delta_| ago and (2) with the same
  // request timeout (to prevent a request with a lower timeout to be pooled with requests with
  // longer timeouts).
  if (auto found_pool =
          std::find_if(pools_.begin(), pools_.end(),
                       [this, creation_time, request_timeout](const CallbackPool& pool) {
                         return (creation_time < pool.creation_time + delta_) &&
                                (request_timeout == pool.request_timeout);
                       });
      found_pool != pools_.end()) {
    found_pool->callbacks.emplace_back(std::move(request_callback));
    return std::nullopt;
  }

  pools_.push_back(CallbackPool{
      .id = next_pool_id_,
      .creation_time = creation_time,
      .request_timeout = request_timeout,
      .callbacks = std::vector<GetSnapshotCallback>(),
  });
  pools_.back().callbacks.emplace_back(std::move(request_callback));
  return next_pool_id_++;
};

void SnapshotRequestManager::Respond(uint64_t id, Snapshot snapshot) {
  auto pool = std::find_if(pools_.begin(), pools_.end(),
                           [id](const CallbackPool& pool) { return pool.id == id; });

  if (pool == pools_.end()) {
    return;
  }

  // We log the pool size as an extra annotation.
  if (!snapshot.has_annotations() ||
      (snapshot.has_annotations() &&
       snapshot.annotations().size() < fuchsia::feedback::MAX_NUM_ANNOTATIONS_PROVIDED)) {
    snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
        .key = kAnnotationDebugPoolSize,
        .value = std::to_string(pool->callbacks.size()),
    });
  }

  for (const auto& callback : pool->callbacks) {
    // The underlying snapshot.zip is shared across all requesters, only its handle is copied.
    Snapshot clone;
    if (const zx_status_t status = snapshot.Clone(&clone); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to clone snapshot";
    }
    callback(std::move(clone));
  }
  pools_.erase(pool);
};

}  // namespace feedback_data
}  // namespace forensics
