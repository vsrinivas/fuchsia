// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_RENDERER_STATE_H_
#define APPS_MOZART_SRC_COMPOSITOR_RENDERER_STATE_H_

#include <memory>
#include <string>

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/src/compositor/backend/output.h"
#include "apps/mozart/src/compositor/frame_dispatcher.h"
#include "apps/mozart/src/compositor/graph/snapshot.h"
#include "apps/mozart/src/compositor/scene_state.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace compositor {

// Describes the state of a particular renderer.
// This object is owned by the CompositorEngine that created it.
class RendererState {
 public:
  RendererState(uint32_t id, const std::string& label);
  ~RendererState();

  ftl::WeakPtr<RendererState> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Sets the associated renderer implementation and takes ownership of it.
  void set_renderer_impl(mozart::Renderer* impl) { renderer_impl_.reset(impl); }

  // The underlying backend output.
  void set_output(std::unique_ptr<Output> output) {
    output_ = std::move(output);
  }
  Output* output() { return output_.get(); }

  // Gets the root scene, may be null if none set yet.
  SceneState* root_scene() { return root_scene_; }
  uint32_t root_scene_version() { return root_scene_version_; }
  const mojo::Rect& root_scene_viewport() { return root_scene_viewport_; }

  // Sets the root scene.
  // If a change occurred, clears the current snapshot and returns true.
  bool SetRootScene(SceneState* scene,
                    uint32_t version,
                    const mojo::Rect& viewport);

  // Removes the root scene.
  // If a change occurred, clears the current snapshot and returns true.
  bool ClearRootScene();

  // The currently visible frame, or null if none.
  ftl::RefPtr<const Snapshot> visible_snapshot() const {
    return visible_snapshot_;
  }

  // The most recent snapshot (which may be blocked from rendering), or
  // null if none.
  ftl::RefPtr<const Snapshot> current_snapshot() const {
    return current_snapshot_;
  }

  // Sets the current snapshot, or null if none.
  // Always updates |current_snapshot()|.
  // If the snapshot is not blocked, also updates |visible_snapshot()|.
  void SetSnapshot(const ftl::RefPtr<const Snapshot>& snapshot);

  FrameDispatcher& frame_dispatcher() { return frame_dispatcher_; }

  const std::string& label() { return label_; }
  std::string FormattedLabel();

 private:
  std::unique_ptr<Output> output_;
  const uint32_t id_;
  const std::string label_;
  std::string formatted_label_cache_;

  FrameDispatcher frame_dispatcher_;  // must be before renderer_impl_
  std::unique_ptr<mozart::Renderer> renderer_impl_;

  SceneState* root_scene_ = nullptr;
  uint32_t root_scene_version_ = mozart::kSceneVersionNone;
  mojo::Rect root_scene_viewport_;

  ftl::RefPtr<const Snapshot> visible_snapshot_;
  ftl::RefPtr<const Snapshot> current_snapshot_;

  ftl::WeakPtrFactory<RendererState> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RendererState);
};

std::ostream& operator<<(std::ostream& os, RendererState* renderer_state);

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_RENDERER_STATE_H_
