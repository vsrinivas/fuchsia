// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_ENGINE_H_
#define APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_ENGINE_H_

#include <unordered_map>
#include <vector>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/src/compositor/frame_info.h"
#include "apps/mozart/src/compositor/graph/universe.h"
#include "apps/mozart/src/compositor/renderer_state.h"
#include "apps/mozart/src/compositor/scene_state.h"
#include "apps/mozart/src/compositor/scheduler.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace compositor {

// Core of the compositor.
// All SceneState and RendererState objects are owned by the engine.
class CompositorEngine {
 public:
  explicit CompositorEngine();
  ~CompositorEngine();

  // COMPOSITOR REQUESTS

  // Registers a scene.
  mozart::SceneTokenPtr CreateScene(
      mojo::InterfaceRequest<mozart::Scene> scene_request,
      const mojo::String& label);

  // Creates a scene graph renderer.
  void CreateRenderer(mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
                      mojo::FramebufferInfoPtr framebuffer_info,
                      mojo::InterfaceRequest<mozart::Renderer> renderer_request,
                      const mojo::String& label);

  // SCENE REQUESTS

  // Sets the scene listener.
  void SetListener(SceneState* scene_state, mozart::SceneListenerPtr listener);

  // Updates a scene.
  // Destroys |scene_state| if an error occurs.
  void Update(SceneState* scene_state, mozart::SceneUpdatePtr update);

  // Publishes a scene.
  // Destroys |scene_state| if an error occurs.
  void Publish(SceneState* scene_state, mozart::SceneMetadataPtr metadata);

  // Schedules a frame callback.
  void ScheduleFrame(SceneState* scene_state, const FrameCallback& callback);

  // RENDERER REQUESTS

  // Sets the root scene.
  // Destroys |renderer_state| if an error occurs.
  void SetRootScene(RendererState* renderer_state,
                    mozart::SceneTokenPtr scene_token,
                    uint32_t scene_version,
                    mojo::RectPtr viewport);

  // Removes the root scene.
  // Destroys |renderer_state| if an error occurs.
  void ClearRootScene(RendererState* renderer_state);

  // Schedules a frame callback.
  void ScheduleFrame(RendererState* renderer_state,
                     const FrameCallback& callback);

  // Performs a hit test.
  void HitTest(RendererState* renderer_state,
               mojo::PointFPtr point,
               const mozart::HitTester::HitTestCallback& callback);

 private:
  void OnSceneConnectionError(SceneState* scene_state);
  void DestroyScene(SceneState* scene_state);

  void OnRendererConnectionError(RendererState* renderer_state);
  void DestroyRenderer(RendererState* renderer_state);

  void InvalidateScene(SceneState* scene_state);
  SceneDef::Disposition PresentScene(SceneState* scene_state,
                                     ftl::TimePoint presentation_time);

  // Starts the process of composing the contents of the renderer to
  // produce a new frame.
  void ComposeRenderer(RendererState* renderer_state,
                       const FrameInfo& frame_info);

  // Applies and validates scene updates from all scenes which are included
  // in the renderer's scene graph.
  void PresentRenderer(RendererState* renderer_state,
                       ftl::TimePoint presentation_time);

  // Resolves scene dependencies and captures a snapshot of the current
  // state of the renderer's scene graph.
  void SnapshotRenderer(RendererState* renderer_state);
  void SnapshotRendererInner(RendererState* renderer_state,
                             std::ostream* block_log);

  // Paints the renderer's current snapshot and submits a frame of content
  // to the output for display.
  void PaintRenderer(RendererState* renderer_state,
                     const FrameInfo& frame_info,
                     ftl::TimePoint composition_time);

  // Schedules the next frame to be rendered, if needed.
  void ScheduleFrameForRenderer(RendererState* renderer_state,
                                Scheduler::SchedulingMode scheduling_mode);

  void OnOutputError(const ftl::WeakPtr<RendererState>& renderer_state_weak);
  void OnOutputUpdateRequest(
      const ftl::WeakPtr<RendererState>& renderer_state_weak,
      const FrameInfo& frame_info);
  void OnOutputSnapshotRequest(
      const ftl::WeakPtr<RendererState>& renderer_state_weak,
      const FrameInfo& frame_info);
  void OnPresentScene(const ftl::WeakPtr<SceneState>& scene_state_weak,
                      ftl::TimePoint presentation_time);

  bool ResolveSceneReference(const mozart::SceneToken& scene_token);
  void SendResourceUnavailable(SceneState* scene_state, uint32_t resource_id);

  SceneState* FindScene(uint32_t scene_token);

  bool IsSceneStateRegisteredDebug(SceneState* scene_state) {
    return scene_state && FindScene(scene_state->scene_token().value);
  }
  bool IsRendererStateRegisteredDebug(RendererState* renderer_state) {
    return renderer_state &&
           std::any_of(renderers_.begin(), renderers_.end(),
                       [renderer_state](RendererState* other) {
                         return renderer_state == other;
                       });
  }

  uint32_t next_scene_token_value_ = 1u;
  uint32_t next_renderer_id_ = 1u;
  std::unordered_map<uint32_t, SceneState*> scenes_by_token_;
  std::vector<RendererState*> renderers_;

  ftl::RefPtr<ftl::TaskRunner> task_runner_;

  Universe universe_;

  ftl::WeakPtrFactory<CompositorEngine> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CompositorEngine);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_COMPOSITOR_ENGINE_H_
