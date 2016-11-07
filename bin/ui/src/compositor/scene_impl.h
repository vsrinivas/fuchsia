// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_SCENE_IMPL_H_
#define APPS_MOZART_SRC_COMPOSITOR_SCENE_IMPL_H_

#include "apps/mozart/services/composition/scenes.fidl.h"
#include "apps/mozart/services/composition/scheduling.fidl.h"
#include "apps/mozart/src/compositor/compositor_engine.h"
#include "apps/mozart/src/compositor/scene_state.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace compositor {

// Scene interface implementation.
// This object is owned by its associated SceneState.
class SceneImpl : public mozart::Scene, public mozart::FrameScheduler {
 public:
  SceneImpl(CompositorEngine* engine,
            SceneState* state,
            fidl::InterfaceRequest<mozart::Scene> scene_request);
  ~SceneImpl() override;

  void set_connection_error_handler(const ftl::Closure& handler) {
    scene_binding_.set_connection_error_handler(handler);
  }

 private:
  // |Scene|:
  void SetListener(
      fidl::InterfaceHandle<mozart::SceneListener> listener) override;
  void Update(mozart::SceneUpdatePtr update) override;
  void Publish(mozart::SceneMetadataPtr metadata) override;
  void GetScheduler(fidl::InterfaceRequest<mozart::FrameScheduler>
                        scheduler_request) override;

  // |FrameScheduler|:
  void ScheduleFrame(const ScheduleFrameCallback& callback) override;

  CompositorEngine* const engine_;
  SceneState* const state_;
  fidl::Binding<mozart::Scene> scene_binding_;
  fidl::BindingSet<mozart::FrameScheduler> scheduler_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SceneImpl);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_SCENE_IMPL_H_
