// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_API_INTERNAL_SNAPSHOT_IMPL_H_
#define SRC_UI_SCENIC_LIB_GFX_API_INTERNAL_SNAPSHOT_IMPL_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>

#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/snapshot/snapshotter.h"

namespace scenic_impl {
namespace gfx {

// Implementation for Scenic's internal snapshot service.
class InternalSnapshotImpl : public fuchsia::ui::scenic::internal::Snapshot {
 public:
  InternalSnapshotImpl(SceneGraphWeakPtr scene_graph, escher::EscherWeakPtr escher)
      : scene_graph_(scene_graph), escher_(escher) {}

  void TakeSnapshot(
      fuchsia::ui::scenic::internal::Snapshot::TakeSnapshotCallback callback) override;

 private:
  // Keeps track of all the returned fuchsia::mem::Buffers and calls the fidl callback
  // once they've all been stored.
  struct PendingSnapshot {
    PendingSnapshot(uint32_t compositors) : num_compositors(compositors) {}

    fuchsia::ui::scenic::internal::Snapshot::TakeSnapshotCallback callback;
    uint32_t num_compositors;
    std::vector<fuchsia::ui::scenic::internal::SnapshotResult> result;

    void AddSnapshot(fuchsia::ui::scenic::internal::SnapshotResult snapshot);
    void InvokeCallback();
  };

  SceneGraphWeakPtr scene_graph_;
  escher::EscherWeakPtr escher_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_API_INTERNAL_SNAPSHOT_IMPL_H_
