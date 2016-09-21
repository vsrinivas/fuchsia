// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_SCENE_STATE_H_
#define SERVICES_GFX_COMPOSITOR_SCENE_STATE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/compositor/services/cpp/formatting.h"
#include "apps/compositor/services/interfaces/scenes.mojom.h"
#include "apps/compositor/src/frame_dispatcher.h"
#include "apps/compositor/src/graph/scene_def.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace compositor {

// Describes the state of a particular scene.
// This object is owned by the CompositorEngine that created it.
class SceneState {
 public:
  SceneState(mojo::gfx::composition::SceneTokenPtr scene_token,
             const std::string& label);
  ~SceneState();

  ftl::WeakPtr<SceneState> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Gets the token used to refer to this scene globally.
  // Caller does not obtain ownership of the token.
  const mojo::gfx::composition::SceneToken& scene_token() {
    return *scene_token_;
  }

  // Gets or sets the scene listener interface.
  mojo::gfx::composition::SceneListener* scene_listener() {
    return scene_listener_.get();
  }
  void set_scene_listener(mojo::gfx::composition::SceneListenerPtr listener) {
    scene_listener_ = listener.Pass();
  }

  // Sets the associated scene implementation and takes ownership of it.
  void set_scene_impl(mojo::gfx::composition::Scene* impl) {
    scene_impl_.reset(impl);
  }

  // Gets the underlying scene definition, never null.
  SceneDef* scene_def() { return &scene_def_; }

  FrameDispatcher& frame_dispatcher() { return frame_dispatcher_; }

 private:
  mojo::gfx::composition::SceneTokenPtr scene_token_;

  FrameDispatcher frame_dispatcher_;  // must be before scene_impl_
  std::unique_ptr<mojo::gfx::composition::Scene> scene_impl_;

  mojo::gfx::composition::SceneListenerPtr scene_listener_;

  SceneDef scene_def_;

  ftl::WeakPtrFactory<SceneState> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneState);
};

std::ostream& operator<<(std::ostream& os, SceneState* scene_state);

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_SCENE_STATE_H_
