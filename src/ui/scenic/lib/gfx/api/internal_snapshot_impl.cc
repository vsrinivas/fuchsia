// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/api/internal_snapshot_impl.h"

namespace scenic_impl {
namespace gfx {

void InternalSnapshotImpl::PendingSnapshot::InvokeCallback() { callback(std::move(result)); }

void InternalSnapshotImpl::PendingSnapshot::AddSnapshot(
    fuchsia::ui::scenic::internal::SnapshotResult snapshot) {
  result.push_back(std::move(snapshot));
  if (result.size() >= num_compositors) {
    FX_DCHECK(result.size() > 0);
    FX_DCHECK(num_compositors > 0);
    InvokeCallback();
  }
}

void InternalSnapshotImpl::TakeSnapshot(
    fuchsia::ui::scenic::internal::Snapshot::TakeSnapshotCallback callback) {
  const auto& compositors = scene_graph_->compositors();

  uint32_t num_compositors = 0;
  for (const auto& compositor : compositors) {
    if (compositor) {
      num_compositors++;
    }
  }

  // Exit early if no valid compositors are found.
  if (num_compositors == 0) {
    callback({});
    return;
  }

  // Create an instance of |PendingSnapshot| which will gather all of the
  // snapshot buffers and invoke the callback when ready.
  auto pending_snapshot = std::make_shared<PendingSnapshot>(num_compositors);
  pending_snapshot->callback = std::move(callback);

  Snapshotter snapshotter(escher_);

  // Loop over each of the compositors and take a snapshot. The resulting buffers
  // are pushed into the caller's result vector. The caller then calls the provided
  // fidl callback function once all of the compositors have been processed.
  for (const auto& compositor : compositors) {
    if (compositor) {
      Resource* resource = compositor.get();
      if (resource) {
        auto push_callback = [pending_snapshot](fuchsia::mem::Buffer buffer, bool success) {
          fuchsia::ui::scenic::internal::SnapshotResult snapshot;
          snapshot.success = success;
          snapshot.buffer = std::move(buffer);
          pending_snapshot->AddSnapshot(std::move(snapshot));
        };

        snapshotter.TakeSnapshot(resource, std::move(push_callback));
      }
    }
  }
}

}  // namespace gfx
}  // namespace scenic_impl
