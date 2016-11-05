// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_SCENE_STATE_H_
#define APPS_MOZART_SRC_COMPOSITOR_SCENE_STATE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/composition/scenes.fidl.h"
#include "apps/mozart/src/compositor/frame_dispatcher.h"
#include "apps/mozart/src/compositor/graph/scene_def.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace compositor {

// Describes the state of a particular scene.
// This object is owned by the CompositorEngine that created it.
class SceneState {
 public:
  SceneState(mozart::SceneTokenPtr scene_token, const std::string& label);
  ~SceneState();

  ftl::WeakPtr<SceneState> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Gets the token used to refer to this scene globally.
  // Caller does not obtain ownership of the token.
  const mozart::SceneToken& scene_token() { return *scene_token_; }

  // Gets or sets the scene listener interface.
  mozart::SceneListener* scene_listener() { return scene_listener_.get(); }
  void set_scene_listener(mozart::SceneListenerPtr listener) {
    scene_listener_ = std::move(listener);
  }

  // Sets the associated scene implementation and takes ownership of it.
  void set_scene_impl(mozart::Scene* impl) { scene_impl_.reset(impl); }

  // Gets the underlying scene definition, never null.
  SceneDef* scene_def() { return &scene_def_; }

  FrameDispatcher& frame_dispatcher() { return frame_dispatcher_; }

 private:
  mozart::SceneTokenPtr scene_token_;

  FrameDispatcher frame_dispatcher_;  // must be before scene_impl_
  std::unique_ptr<mozart::Scene> scene_impl_;

  mozart::SceneListenerPtr scene_listener_;

  SceneDef scene_def_;

  ftl::WeakPtrFactory<SceneState> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneState);
};

std::ostream& operator<<(std::ostream& os, SceneState* scene_state);

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_SCENE_STATE_H_
