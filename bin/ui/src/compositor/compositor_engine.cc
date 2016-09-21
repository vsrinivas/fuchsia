// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/compositor_engine.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "apps/compositor/glue/base/logging.h"
#include "apps/compositor/glue/base/trace_event.h"
#include "apps/compositor/glue/skia/type_converters.h"
#include "apps/compositor/services/cpp/formatting.h"
#include "apps/compositor/src/backend/gpu_output.h"
#include "apps/compositor/src/graph/snapshot.h"
#include "apps/compositor/src/render/render_frame.h"
#include "apps/compositor/src/renderer_impl.h"
#include "apps/compositor/src/scene_impl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/tasks/message_loop.h"

namespace compositor {
namespace {
// TODO(jeffbrown): Determine and document a more appropriate size limit
// for viewports somewhere.  May be limited by the renderer output.
const int32_t kMaxViewportWidth = 65536;
const int32_t kMaxViewportHeight = 65536;

std::string SanitizeLabel(const mojo::String& label) {
  return label.get().substr(0, mojo::gfx::composition::kLabelMaxLength);
}
}  // namespace

CompositorEngine::CompositorEngine() : weak_factory_(this) {}

CompositorEngine::~CompositorEngine() {}

mojo::gfx::composition::SceneTokenPtr CompositorEngine::CreateScene(
    mojo::InterfaceRequest<mojo::gfx::composition::Scene> scene_request,
    const mojo::String& label) {
  auto scene_token = mojo::gfx::composition::SceneToken::New();
  scene_token->value = next_scene_token_value_++;
  FTL_CHECK(scene_token->value);
  FTL_CHECK(!FindScene(scene_token->value));

  // Create the state and bind implementation to it.
  SceneState* scene_state =
      new SceneState(scene_token.Pass(), SanitizeLabel(label));
  SceneImpl* scene_impl =
      new SceneImpl(this, scene_state, scene_request.Pass());
  scene_state->set_scene_impl(scene_impl);
  ftl::Closure error_handler = [this, scene_state] {
    OnSceneConnectionError(scene_state);
  };
  scene_impl->set_connection_error_handler(error_handler);

  // Add to registry.
  scenes_by_token_.emplace(scene_state->scene_token().value, scene_state);
  universe_.AddScene(scene_state->scene_def()->label());
  DVLOG(1) << "CreateScene: scene=" << scene_state;
  return scene_state->scene_token().Clone();
}

void CompositorEngine::OnSceneConnectionError(SceneState* scene_state) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "OnSceneConnectionError: scene=" << scene_state;

  DestroyScene(scene_state);
}

void CompositorEngine::DestroyScene(SceneState* scene_state) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "DestroyScene: scene=" << scene_state;

  // Notify other scenes which may depend on this one.
  for (auto& pair : scenes_by_token_) {
    SceneState* other_scene_state = pair.second;
    other_scene_state->scene_def()->NotifySceneUnavailable(
        scene_state->scene_token(),
        [this, other_scene_state](uint32_t resource_id) {
          SendResourceUnavailable(other_scene_state, resource_id);
        });
  }

  // Destroy any renderers using this scene.
  std::vector<RendererState*> renderers_to_destroy;
  for (auto& renderer : renderers_) {
    if (renderer->root_scene() == scene_state) {
      renderers_to_destroy.emplace_back(renderer);
    }
  }
  for (auto& renderer : renderers_to_destroy) {
    FTL_LOG(ERROR) << "Destroying renderer whose root scene has become "
                      "unavailable: renderer="
                   << renderer;
    DestroyRenderer(renderer);
  }

  // Consider all dependent rendering to be invalidated.
  universe_.RemoveScene(scene_state->scene_token());
  InvalidateScene(scene_state);

  // Remove from registry.
  scenes_by_token_.erase(scene_state->scene_token().value);
  delete scene_state;
}

void CompositorEngine::CreateRenderer(
    mojo::InterfaceHandle<mojo::ContextProvider> context_provider,
    mojo::InterfaceRequest<mojo::gfx::composition::Renderer> renderer_request,
    const mojo::String& label) {
  FTL_DCHECK(context_provider);
  uint32_t renderer_id = next_renderer_id_++;
  FTL_CHECK(renderer_id);

  // Create the state and bind implementation to it.
  RendererState* renderer_state =
      new RendererState(renderer_id, SanitizeLabel(label));
  RendererImpl* renderer_impl =
      new RendererImpl(this, renderer_state, renderer_request.Pass());
  renderer_state->set_renderer_impl(renderer_impl);
  renderer_impl->set_connection_error_handler(
      [this, renderer_state] { OnRendererConnectionError(renderer_state); });

  // Create the renderer.
  SchedulerCallbacks scheduler_callbacks(
      [
        weak = weak_factory_.GetWeakPtr(),
        renderer_state_weak = renderer_state->GetWeakPtr()
      ](const mojo::gfx::composition::FrameInfo& frame_info) {
        if (weak)
          weak->OnOutputUpdateRequest(renderer_state_weak, frame_info);
      },
      [
        weak = weak_factory_.GetWeakPtr(),
        renderer_state_weak = renderer_state->GetWeakPtr()
      ](const mojo::gfx::composition::FrameInfo& frame_info) {
        if (weak)
          weak->OnOutputSnapshotRequest(renderer_state_weak, frame_info);
      });
  std::unique_ptr<Output> output(
      new GpuOutput(context_provider.Pass(), scheduler_callbacks, [
        weak = weak_factory_.GetWeakPtr(),
        renderer_state_weak = renderer_state->GetWeakPtr()
      ] {
        if (weak)
          weak->OnOutputError(renderer_state_weak);
      }));
  renderer_state->set_output(std::move(output));

  // Add to registry.
  renderers_.push_back(renderer_state);
  DVLOG(1) << "CreateRenderer: " << renderer_state;
}

void CompositorEngine::OnRendererConnectionError(
    RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(1) << "OnRendererConnectionError: renderer=" << renderer_state;

  DestroyRenderer(renderer_state);
}

void CompositorEngine::DestroyRenderer(RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(1) << "DestroyRenderer: renderer=" << renderer_state;

  // Remove from registry.
  renderers_.erase(
      std::find(renderers_.begin(), renderers_.end(), renderer_state));
  delete renderer_state;
}

void CompositorEngine::SetListener(
    SceneState* scene_state,
    mojo::gfx::composition::SceneListenerPtr listener) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "SetSceneListener: scene=" << scene_state;

  scene_state->set_scene_listener(listener.Pass());
}

void CompositorEngine::Update(SceneState* scene_state,
                              mojo::gfx::composition::SceneUpdatePtr update) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "Update: scene=" << scene_state << ", update=" << update;

  scene_state->scene_def()->EnqueueUpdate(update.Pass());
}

void CompositorEngine::Publish(
    SceneState* scene_state,
    mojo::gfx::composition::SceneMetadataPtr metadata) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "Publish: scene=" << scene_state << ", metadata=" << metadata;

  if (!metadata)
    metadata = mojo::gfx::composition::SceneMetadata::New();
  int64_t presentation_time = metadata->presentation_time;
  scene_state->scene_def()->EnqueuePublish(metadata.Pass());

  // Implicitly schedule fresh snapshots.
  InvalidateScene(scene_state);

  // Ensure that the scene will be presented eventually, even if it is
  // not associated with any renderer.  Note that this is only a backstop
  // in case the scene does not get presented sooner as part of snapshotting
  // a renderer.  Note that scenes which are actually visible will be
  // snapshotted by the renderer when it comes time to draw the next frame,
  // so this special case is only designed to help with scenes that are
  // not visible to ensure that we will still apply pending updates which
  // might have side-effects on the client's state (such as closing the
  // connection due to an error or releasing resources).
  MojoTimeTicks now = MojoGetTimeTicksNow();
  FTL_DCHECK(now >= 0);
  if (presentation_time <= now) {
    SceneDef::Disposition disposition = PresentScene(scene_state, now);
    if (disposition == SceneDef::Disposition::kFailed)
      DestroyScene(scene_state);
  } else {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [
          weak = weak_factory_.GetWeakPtr(),
          scene_state_weak = scene_state->GetWeakPtr(), presentation_time
        ] {
          if (weak)
            weak->OnPresentScene(scene_state_weak, presentation_time);
        },
        ftl::TimeDelta::FromMicroseconds(presentation_time - now));
  }
}

void CompositorEngine::ScheduleFrame(SceneState* scene_state,
                                     const FrameCallback& callback) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(1) << "ScheduleFrame: scene=" << scene_state;

  if (!scene_state->frame_dispatcher().AddCallback(callback))
    return;

  // TODO(jeffbrown): Be more selective and do this work only for scenes
  // which are strongly associated with the renderer so it doesn't receive
  // conflicting timing signals coming from multiple renderers.
  for (auto& renderer : renderers_) {
    ScheduleFrameForRenderer(renderer,
                             Scheduler::SchedulingMode::kUpdateAndSnapshot);
  }
}

void CompositorEngine::SetRootScene(
    RendererState* renderer_state,
    mojo::gfx::composition::SceneTokenPtr scene_token,
    uint32_t scene_version,
    mojo::RectPtr viewport) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_DCHECK(scene_token);
  FTL_DCHECK(viewport);
  DVLOG(1) << "SetRootScene: renderer=" << renderer_state
           << ", scene_token=" << scene_token
           << ", scene_version=" << scene_version << ", viewport=" << viewport;

  if (viewport->width <= 0 || viewport->width > kMaxViewportWidth ||
      viewport->height <= 0 || viewport->height > kMaxViewportHeight) {
    FTL_LOG(ERROR) << "Invalid viewport size: " << viewport;
    DestroyRenderer(renderer_state);
    return;
  }

  // Find the scene.
  SceneState* scene_state = FindScene(scene_token->value);
  if (!scene_state) {
    FTL_LOG(ERROR)
        << "Could not set the renderer's root scene, scene not found: "
           "scene_token="
        << scene_token;
    DestroyRenderer(renderer_state);
    return;
  }

  // Update the root.
  if (renderer_state->SetRootScene(scene_state, scene_version, *viewport)) {
    ScheduleFrameForRenderer(renderer_state,
                             Scheduler::SchedulingMode::kSnapshot);
  }
}

void CompositorEngine::ClearRootScene(RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(1) << "ClearRootScene: renderer=" << renderer_state;

  // Update the root.
  if (renderer_state->ClearRootScene()) {
    ScheduleFrameForRenderer(renderer_state,
                             Scheduler::SchedulingMode::kSnapshot);
  }
}

void CompositorEngine::ScheduleFrame(RendererState* renderer_state,
                                     const FrameCallback& callback) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(1) << "ScheduleFrame: renderer=" << renderer_state;

  if (!renderer_state->frame_dispatcher().AddCallback(callback))
    return;

  ScheduleFrameForRenderer(renderer_state,
                           Scheduler::SchedulingMode::kUpdateAndSnapshot);
}

void CompositorEngine::HitTest(
    RendererState* renderer_state,
    mojo::PointFPtr point,
    const mojo::gfx::composition::HitTester::HitTestCallback& callback) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_DCHECK(point);
  DVLOG(1) << "HitTest: renderer=" << renderer_state << ", point=" << point;

  auto result = mojo::gfx::composition::HitTestResult::New();

  if (renderer_state->visible_snapshot()) {
    FTL_DCHECK(!renderer_state->visible_snapshot()->is_blocked());
    renderer_state->visible_snapshot()->HitTest(*point, result.get());
  }

  callback.Run(result.Pass());
}

bool CompositorEngine::ResolveSceneReference(
    const mojo::gfx::composition::SceneToken& scene_token) {
  return FindScene(scene_token.value) != nullptr;
}

void CompositorEngine::SendResourceUnavailable(SceneState* scene_state,
                                               uint32_t resource_id) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(2) << "SendResourceUnavailable: resource_id=" << resource_id;

  // TODO: Detect ANRs
  if (scene_state->scene_listener()) {
    scene_state->scene_listener()->OnResourceUnavailable(resource_id, [] {});
  }
}

SceneState* CompositorEngine::FindScene(uint32_t scene_token) {
  auto it = scenes_by_token_.find(scene_token);
  return it != scenes_by_token_.end() ? it->second : nullptr;
}

void CompositorEngine::InvalidateScene(SceneState* scene_state) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(2) << "InvalidateScene: scene=" << scene_state;

  for (auto& renderer : renderers_) {
    if (renderer->current_snapshot() &&
        renderer->current_snapshot()->HasDependency(
            scene_state->scene_token())) {
      ScheduleFrameForRenderer(renderer, Scheduler::SchedulingMode::kSnapshot);
    }
  }
}

SceneDef::Disposition CompositorEngine::PresentScene(
    SceneState* scene_state,
    int64_t presentation_time) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  DVLOG(2) << "PresentScene: scene=" << scene_state;

  std::ostringstream errs;
  SceneDef::Disposition disposition = scene_state->scene_def()->Present(
      presentation_time, &universe_,
      [this](const mojo::gfx::composition::SceneToken& scene_token) {
        return ResolveSceneReference(scene_token);
      },
      [this, scene_state](uint32_t resource_id) {
        return SendResourceUnavailable(scene_state, resource_id);
      },
      errs);
  if (disposition == SceneDef::Disposition::kFailed) {
    FTL_LOG(ERROR) << "Scene published invalid updates: scene=" << scene_state;
    FTL_LOG(ERROR) << errs.str();
    // Caller is responsible for destroying the scene.
  }
  return disposition;
}

void CompositorEngine::ComposeRenderer(
    RendererState* renderer_state,
    const mojo::gfx::composition::FrameInfo& frame_info) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(2) << "ComposeRenderer: renderer_state=" << renderer_state;

  TRACE_EVENT1("gfx", "CompositorEngine::ComposeRenderer", "renderer",
               renderer_state->FormattedLabel());

  int64_t composition_time = MojoGetTimeTicksNow();
  PresentRenderer(renderer_state, frame_info.presentation_time);
  SnapshotRenderer(renderer_state);
  PaintRenderer(renderer_state, frame_info, composition_time);
}

void CompositorEngine::PresentRenderer(RendererState* renderer_state,
                                       int64_t presentation_time) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(2) << "PresentRenderer: renderer_state=" << renderer_state;

  TRACE_EVENT1("gfx", "CompositorEngine::PresentRenderer", "renderer",
               renderer_state->FormattedLabel());

  // TODO(jeffbrown): Be more selective and do this work only for scenes
  // associated with the renderer that actually have pending updates.
  std::vector<SceneState*> dead_scenes;
  for (auto& pair : scenes_by_token_) {
    SceneState* scene_state = pair.second;
    SceneDef::Disposition disposition =
        PresentScene(scene_state, presentation_time);
    if (disposition == SceneDef::Disposition::kFailed)
      dead_scenes.push_back(scene_state);
  }
  for (SceneState* scene_state : dead_scenes)
    DestroyScene(scene_state);
}

void CompositorEngine::SnapshotRenderer(RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(2) << "SnapshotRenderer: renderer_state=" << renderer_state;

  TRACE_EVENT1("gfx", "CompositorEngine::SnapshotRenderer", "renderer",
               renderer_state->FormattedLabel());

  if (VLOG_IS_ON(2)) {
    std::ostringstream block_log;
    SnapshotRendererInner(renderer_state, &block_log);
    if (!renderer_state->current_snapshot() ||
        renderer_state->current_snapshot()->is_blocked()) {
      DVLOG(2) << "Rendering completely blocked:" << std::endl
               << block_log.str();
    } else if (!block_log.str().empty()) {
      DVLOG(2) << "Rendering partially blocked:" << std::endl
               << block_log.str();
    } else {
      DVLOG(2) << "Rendering unblocked";
    }
  } else {
    SnapshotRendererInner(renderer_state, nullptr);
  }
}

void CompositorEngine::SnapshotRendererInner(RendererState* renderer_state,
                                             std::ostream* block_log) {
  if (!renderer_state->root_scene()) {
    if (block_log)
      *block_log << "No root scene" << std::endl;
    renderer_state->SetSnapshot(nullptr);
    return;
  }

  renderer_state->SetSnapshot(
      universe_.SnapshotScene(renderer_state->root_scene()->scene_token(),
                              renderer_state->root_scene_version(), block_log));
}

void CompositorEngine::PaintRenderer(
    RendererState* renderer_state,
    const mojo::gfx::composition::FrameInfo& frame_info,
    int64_t composition_time) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  DVLOG(2) << "PaintRenderer: renderer_state=" << renderer_state;

  TRACE_EVENT1("gfx", "CompositorEngine::PaintRenderer", "renderer",
               renderer_state->FormattedLabel());

  RenderFrame::Metadata frame_metadata(frame_info, composition_time);

  if (renderer_state->visible_snapshot()) {
    // The renderer has snapshotted content; paint and submit it.
    FTL_DCHECK(!renderer_state->visible_snapshot()->is_blocked());
    renderer_state->output()->SubmitFrame(
        renderer_state->visible_snapshot()->Paint(
            frame_metadata, renderer_state->root_scene_viewport()));
  } else {
    // The renderer does not have any content; submit an empty (black) frame.
    SkIRect viewport = renderer_state->root_scene_viewport().To<SkIRect>();
    if (!viewport.isEmpty()) {
      renderer_state->output()->SubmitFrame(
          ftl::MakeRefCounted<RenderFrame>(frame_metadata, viewport));
    }
  }
}

void CompositorEngine::ScheduleFrameForRenderer(
    RendererState* renderer_state,
    Scheduler::SchedulingMode scheduling_mode) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  renderer_state->output()->GetScheduler()->ScheduleFrame(scheduling_mode);
}

void CompositorEngine::OnOutputError(
    const ftl::WeakPtr<RendererState>& renderer_state_weak) {
  RendererState* renderer_state = renderer_state_weak.get();
  if (!renderer_state)
    return;
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));

  FTL_LOG(ERROR) << "Renderer encountered a fatal error: renderer="
                 << renderer_state;

  DestroyRenderer(renderer_state);
}

void CompositorEngine::OnOutputUpdateRequest(
    const ftl::WeakPtr<RendererState>& renderer_state_weak,
    const mojo::gfx::composition::FrameInfo& frame_info) {
  RendererState* renderer_state = renderer_state_weak.get();
  if (!renderer_state)
    return;
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));

  renderer_state->frame_dispatcher().DispatchCallbacks(frame_info);

  // TODO(jeffbrown): Be more selective and do this work only for scenes
  // associated with the renderer.
  for (auto& pair : scenes_by_token_) {
    pair.second->frame_dispatcher().DispatchCallbacks(frame_info);
  }
}

void CompositorEngine::OnOutputSnapshotRequest(
    const ftl::WeakPtr<RendererState>& renderer_state_weak,
    const mojo::gfx::composition::FrameInfo& frame_info) {
  RendererState* renderer_state = renderer_state_weak.get();
  if (!renderer_state)
    return;
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));

  ComposeRenderer(renderer_state, frame_info);
}

void CompositorEngine::OnPresentScene(
    const ftl::WeakPtr<SceneState>& scene_state_weak,
    int64_t presentation_time) {
  SceneState* scene_state = scene_state_weak.get();
  if (!scene_state)
    return;
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));

  SceneDef::Disposition disposition =
      PresentScene(scene_state, presentation_time);
  if (disposition == SceneDef::Disposition::kFailed)
    DestroyScene(scene_state);
  else if (disposition == SceneDef::Disposition::kSucceeded)
    InvalidateScene(scene_state);
}

}  // namespace compositor
