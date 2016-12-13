// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/compositor_engine.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "apps/tracing/lib/trace/event.h"
#include "apps/mozart/lib/skia/type_converters.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/src/compositor/backend/framebuffer_output.h"
#include "apps/mozart/src/compositor/graph/snapshot.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/mozart/src/compositor/renderer_impl.h"
#include "apps/mozart/src/compositor/scene_impl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/tasks/message_loop.h"

namespace compositor {
namespace {
// TODO(jeffbrown): Determine and document a more appropriate size limit
// for viewports somewhere.  May be limited by the renderer output.
const int32_t kMaxViewportWidth = 65536;
const int32_t kMaxViewportHeight = 65536;

std::string SanitizeLabel(const fidl::String& label) {
  return label.get().substr(0, mozart::Compositor::kLabelMaxLength);
}
}  // namespace

CompositorEngine::CompositorEngine()
    : task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      weak_factory_(this) {}

CompositorEngine::~CompositorEngine() {}

void CompositorEngine::Dump(std::unique_ptr<tracing::Dump> dump) {
  FTL_DCHECK(dump);

  dump->out() << "Compositor dump..." << std::endl;

  dump->out() << std::endl << "RENDERERS" << std::endl;
  for (RendererState* renderer : renderers_) {
    dump->out() << "  " << renderer->FormattedLabel() << std::endl;
    dump->out() << "    root_scene=" << renderer->root_scene() << std::endl;
    dump->out() << "    root_scene_version=" << renderer->root_scene_version()
                << std::endl;
    dump->out() << "    root_scene_viewport=" << renderer->root_scene_viewport()
                << std::endl;
  }

  dump->out() << std::endl << "SCENES" << std::endl;
  for (auto item : scenes_by_token_) {
    SceneDef* scene_def = item.second->scene_def();
    dump->out() << "  " << scene_def->FormattedLabel() << std::endl;
    scene_def->Dump(dump.get(), "    ");
  }
}

mozart::SceneTokenPtr CompositorEngine::CreateScene(
    fidl::InterfaceRequest<mozart::Scene> scene_request,
    const fidl::String& label) {
  auto scene_token = mozart::SceneToken::New();
  scene_token->value = next_scene_token_value_++;
  FTL_CHECK(scene_token->value);
  FTL_CHECK(!FindScene(scene_token->value));

  // Create the state and bind implementation to it.
  SceneState* scene_state =
      new SceneState(std::move(scene_token), SanitizeLabel(label));
  SceneImpl* scene_impl =
      new SceneImpl(this, scene_state, std::move(scene_request));
  scene_state->set_scene_impl(scene_impl);
  ftl::Closure error_handler = [this, scene_state] {
    OnSceneConnectionError(scene_state);
  };
  scene_impl->set_connection_error_handler(error_handler);

  // Add to registry.
  scenes_by_token_.emplace(scene_state->scene_token().value, scene_state);
  universe_.AddScene(scene_state->scene_def()->label());
  FTL_VLOG(1) << "CreateScene: scene=" << scene_state;
  return scene_state->scene_token().Clone();
}

void CompositorEngine::OnSceneConnectionError(SceneState* scene_state) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "OnSceneConnectionError: scene=" << scene_state;

  DestroyScene(scene_state);
}

void CompositorEngine::DestroyScene(SceneState* scene_state) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "DestroyScene: scene=" << scene_state;

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
    fidl::InterfaceRequest<mozart::Renderer> renderer_request,
    const fidl::String& label) {
  uint32_t renderer_id = next_renderer_id_++;
  FTL_CHECK(renderer_id);

  // Create the state and bind implementation to it.
  RendererState* renderer_state = new RendererState(
      renderer_id, SanitizeLabel(label), std::make_unique<FramebufferOutput>());
  RendererImpl* renderer_impl =
      new RendererImpl(this, renderer_state, std::move(renderer_request));
  renderer_state->set_renderer_impl(renderer_impl);
  renderer_impl->set_connection_error_handler(
      [this, renderer_state] { OnRendererConnectionError(renderer_state); });

  // Bind scheduler callbacks.
  renderer_state->scheduler()->SetCallbacks(
      [
        weak = weak_factory_.GetWeakPtr(),
        renderer_state_weak = renderer_state->GetWeakPtr()
      ](const FrameInfo& frame_info) {
        if (weak)
          weak->OnOutputUpdateRequest(renderer_state_weak, frame_info);
      },
      [
        weak = weak_factory_.GetWeakPtr(),
        renderer_state_weak = renderer_state->GetWeakPtr()
      ](const FrameInfo& frame_info) {
        if (weak)
          weak->OnOutputSnapshotRequest(renderer_state_weak, frame_info);
      });

  // Initialize the output.
  static_cast<FramebufferOutput*>(renderer_state->output())->Initialize([
    weak = weak_factory_.GetWeakPtr(),
    renderer_state_weak = renderer_state->GetWeakPtr()
  ] {
    if (weak)
      weak->OnOutputError(renderer_state_weak);
  });

  // Add to registry.
  renderers_.push_back(renderer_state);
  FTL_VLOG(1) << "CreateRenderer: " << renderer_state;
}

void CompositorEngine::OnRendererConnectionError(
    RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(1) << "OnRendererConnectionError: renderer=" << renderer_state;

  DestroyRenderer(renderer_state);
}

void CompositorEngine::DestroyRenderer(RendererState* renderer_state) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(1) << "DestroyRenderer: renderer=" << renderer_state;

  // Remove from registry.
  renderers_.erase(
      std::find(renderers_.begin(), renderers_.end(), renderer_state));
  delete renderer_state;
}

void CompositorEngine::SetListener(SceneState* scene_state,
                                   mozart::SceneListenerPtr listener) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "SetSceneListener: scene=" << scene_state;

  scene_state->set_scene_listener(std::move(listener));
}

void CompositorEngine::Update(SceneState* scene_state,
                              mozart::SceneUpdatePtr update) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "Update: scene=" << scene_state << ", update=" << update;

  scene_state->scene_def()->EnqueueUpdate(std::move(update));
}

void CompositorEngine::Publish(SceneState* scene_state,
                               mozart::SceneMetadataPtr metadata) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "Publish: scene=" << scene_state << ", metadata=" << metadata;

  if (!metadata)
    metadata = mozart::SceneMetadata::New();
  ftl::TimePoint presentation_time = ftl::TimePoint::FromEpochDelta(
      ftl::TimeDelta::FromNanoseconds(metadata->presentation_time));
  scene_state->scene_def()->EnqueuePublish(std::move(metadata));

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
  ftl::TimePoint now = ftl::TimePoint::Now();
  if (presentation_time <= now) {
    SceneDef::Disposition disposition = PresentScene(scene_state, now);
    if (disposition == SceneDef::Disposition::kFailed)
      DestroyScene(scene_state);
  } else {
    task_runner_->PostTaskForTime(
        [
          weak = weak_factory_.GetWeakPtr(),
          scene_state_weak = scene_state->GetWeakPtr(), presentation_time
        ] {
          if (weak)
            weak->OnPresentScene(scene_state_weak, presentation_time);
        },
        presentation_time);
  }
}

void CompositorEngine::ScheduleFrame(SceneState* scene_state,
                                     const FrameCallback& callback) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(1) << "ScheduleFrame: scene=" << scene_state;

  if (!scene_state->frame_dispatcher().AddCallback(callback))
    return;

  // TODO(jeffbrown): Be more selective and do this work only for scenes
  // which are strongly associated with the renderer so it doesn't receive
  // conflicting timing signals coming from multiple renderers.
  for (auto& renderer : renderers_) {
    ScheduleFrameForRenderer(renderer,
                             Scheduler::SchedulingMode::kUpdateThenSnapshot);
  }
}

void CompositorEngine::GetDisplayInfo(RendererState* renderer_state,
                                      DisplayCallback callback) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(1) << "GetDisplayInfo: renderer=" << renderer_state;

  renderer_state->output()->GetDisplayInfo(std::move(callback));
}

void CompositorEngine::SetRootScene(RendererState* renderer_state,
                                    mozart::SceneTokenPtr scene_token,
                                    uint32_t scene_version,
                                    mozart::RectPtr viewport) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_DCHECK(scene_token);
  FTL_DCHECK(viewport);
  FTL_VLOG(1) << "SetRootScene: renderer=" << renderer_state
              << ", scene_token=" << scene_token
              << ", scene_version=" << scene_version
              << ", viewport=" << viewport;

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
  FTL_VLOG(1) << "ClearRootScene: renderer=" << renderer_state;

  // Update the root.
  if (renderer_state->ClearRootScene()) {
    ScheduleFrameForRenderer(renderer_state,
                             Scheduler::SchedulingMode::kSnapshot);
  }
}

void CompositorEngine::ScheduleFrame(RendererState* renderer_state,
                                     const FrameCallback& callback) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(1) << "ScheduleFrame: renderer=" << renderer_state;

  if (!renderer_state->frame_dispatcher().AddCallback(callback))
    return;

  ScheduleFrameForRenderer(renderer_state,
                           Scheduler::SchedulingMode::kUpdateThenSnapshot);
}

void CompositorEngine::HitTest(
    RendererState* renderer_state,
    mozart::PointFPtr point,
    const mozart::HitTester::HitTestCallback& callback) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_DCHECK(point);
  FTL_VLOG(1) << "HitTest: renderer=" << renderer_state << ", point=" << point;

  auto result = mozart::HitTestResult::New();

  if (renderer_state->visible_snapshot()) {
    FTL_DCHECK(!renderer_state->visible_snapshot()->is_blocked());
    renderer_state->visible_snapshot()->HitTest(*point, result.get());
  }

  callback(std::move(result));
}

bool CompositorEngine::ResolveSceneReference(
    const mozart::SceneToken& scene_token) {
  return FindScene(scene_token.value) != nullptr;
}

void CompositorEngine::SendResourceUnavailable(SceneState* scene_state,
                                               uint32_t resource_id) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(2) << "SendResourceUnavailable: resource_id=" << resource_id;

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
  FTL_VLOG(2) << "InvalidateScene: scene=" << scene_state;

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
    ftl::TimePoint presentation_time) {
  FTL_DCHECK(IsSceneStateRegisteredDebug(scene_state));
  FTL_VLOG(2) << "PresentScene: scene=" << scene_state;

  std::ostringstream errs;
  SceneDef::Disposition disposition = scene_state->scene_def()->Present(
      presentation_time, &universe_,
      [this](const mozart::SceneToken& scene_token) {
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

void CompositorEngine::ComposeRenderer(RendererState* renderer_state,
                                       const FrameInfo& frame_info) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(2) << "ComposeRenderer: renderer_state=" << renderer_state;

  TRACE_DURATION("gfx", "CompositorEngine::ComposeRenderer", "renderer",
                  renderer_state->FormattedLabel());

  ftl::TimePoint composition_time = ftl::TimePoint();
  PresentRenderer(renderer_state, frame_info.presentation_time);
  SnapshotRenderer(renderer_state);
  PaintRenderer(renderer_state, frame_info, composition_time);
}

void CompositorEngine::PresentRenderer(RendererState* renderer_state,
                                       ftl::TimePoint presentation_time) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(2) << "PresentRenderer: renderer_state=" << renderer_state;

  TRACE_DURATION("gfx", "CompositorEngine::PresentRenderer", "renderer",
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
  FTL_VLOG(2) << "SnapshotRenderer: renderer_state=" << renderer_state;

  TRACE_DURATION("gfx", "CompositorEngine::SnapshotRenderer", "renderer",
                  renderer_state->FormattedLabel());

  if (FTL_VLOG_IS_ON(2)) {
    std::ostringstream block_log;
    SnapshotRendererInner(renderer_state, &block_log);
    if (!renderer_state->current_snapshot() ||
        renderer_state->current_snapshot()->is_blocked()) {
      FTL_VLOG(2) << "Rendering completely blocked:" << std::endl
                  << block_log.str();
    } else if (!block_log.str().empty()) {
      FTL_VLOG(2) << "Rendering partially blocked:" << std::endl
                  << block_log.str();
    } else {
      FTL_VLOG(2) << "Rendering unblocked";
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

void CompositorEngine::PaintRenderer(RendererState* renderer_state,
                                     const FrameInfo& frame_info,
                                     ftl::TimePoint composition_time) {
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));
  FTL_VLOG(2) << "PaintRenderer: renderer_state=" << renderer_state;

  TRACE_DURATION("gfx", "CompositorEngine::PaintRenderer", "renderer",
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
  renderer_state->scheduler()->ScheduleFrame(scheduling_mode);
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
    const FrameInfo& frame_info) {
  RendererState* renderer_state = renderer_state_weak.get();
  if (!renderer_state)
    return;
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));

  mozart::FrameInfo dispatched_frame_info;
  dispatched_frame_info.presentation_time =
      frame_info.presentation_time.ToEpochDelta().ToNanoseconds();
  dispatched_frame_info.presentation_interval =
      frame_info.presentation_interval.ToNanoseconds();
  dispatched_frame_info.publish_deadline =
      frame_info.publish_deadline.ToEpochDelta().ToNanoseconds();
  dispatched_frame_info.base_time =
      frame_info.base_time.ToEpochDelta().ToNanoseconds();

  renderer_state->frame_dispatcher().DispatchCallbacks(dispatched_frame_info);

  // TODO(jeffbrown): Be more selective and do this work only for scenes
  // associated with the renderer.
  for (auto& pair : scenes_by_token_) {
    pair.second->frame_dispatcher().DispatchCallbacks(dispatched_frame_info);
  }
}

void CompositorEngine::OnOutputSnapshotRequest(
    const ftl::WeakPtr<RendererState>& renderer_state_weak,
    const FrameInfo& frame_info) {
  RendererState* renderer_state = renderer_state_weak.get();
  if (!renderer_state)
    return;
  FTL_DCHECK(IsRendererStateRegisteredDebug(renderer_state));

  ComposeRenderer(renderer_state, frame_info);
}

void CompositorEngine::OnPresentScene(
    const ftl::WeakPtr<SceneState>& scene_state_weak,
    ftl::TimePoint presentation_time) {
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
