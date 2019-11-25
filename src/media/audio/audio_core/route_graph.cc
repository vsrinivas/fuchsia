// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/route_graph.h"

#include <algorithm>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

RouteGraph::RouteGraph(const RoutingConfig& routing_config) : routing_config_(routing_config) {}

RouteGraph::~RouteGraph() {
  if (throttle_release_fence_) {
    throttle_release_fence_->complete_ok();
  }
}

void RouteGraph::SetThrottleOutput(ThreadingModel* threading_model,
                                   fbl::RefPtr<AudioOutput> throttle_output) {
  fit::bridge<void, void> bridge;
  threading_model->FidlDomain().ScheduleTask(bridge.consumer.promise().then(
      [throttle_output](fit::result<void, void>& _) { return throttle_output->Shutdown(); }));

  threading_model->FidlDomain().executor()->schedule_task(
      throttle_output->Startup().or_else([throttle_output](zx_status_t& error) {
        FX_PLOGS(ERROR, error) << "Failed to initialize the throttle output";
        return throttle_output->Shutdown();
      }));

  throttle_release_fence_ = {std::move(bridge.completer)};
  throttle_output_ = throttle_output;
  AddOutput(throttle_output_.get());
}

void RouteGraph::AddOutput(AudioDevice* output) {
  AUD_VLOG(TRACE) << "Added output device to route graph: " << output;

  outputs_.push_front(output);
  UpdateGraphForDevicesChange();
}

void RouteGraph::RemoveOutput(AudioDevice* output) {
  AUD_VLOG(TRACE) << "Removing output device from graph: " << output;

  auto it = std::find(outputs_.begin(), outputs_.end(), output);
  if (it == outputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered output device from the route graph.";
    return;
  }

  outputs_.erase(it);
  UpdateGraphForDevicesChange();
}

void RouteGraph::AddInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Added input device to route graph: " << input;

  inputs_.push_front(input);
  UpdateGraphForDevicesChange();
}

void RouteGraph::RemoveInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Removing input device to route graph: " << input;

  auto it = std::find(inputs_.begin(), inputs_.end(), input);
  if (it == inputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered input device from the route graph.";
    return;
  }

  inputs_.erase(it);
  UpdateGraphForDevicesChange();
}

void RouteGraph::AddRenderer(fbl::RefPtr<AudioObject> renderer) {
  FX_DCHECK(throttle_output_);
  FX_DCHECK(renderer->is_audio_renderer());
  AUD_VLOG(TRACE) << "Adding renderer route graph: " << renderer.get();

  renderers_.insert({renderer.get(), RoutableOwnedObject{renderer, {}}});
}

void RouteGraph::SetRendererRoutingProfile(AudioObject* renderer, RoutingProfile profile) {
  FX_DCHECK(renderer->is_audio_renderer());
  FX_CHECK(renderer->format_valid() || !profile.routable)
      << "AudioRenderer without PCM format was added to route graph";
  AUD_VLOG(TRACE) << "Setting renderer route profile: " << renderer;

  auto it = renderers_.find(renderer);
  if (it == renderers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_render_usage()) {
    it->second.ref->Unlink();
    return;
  }

  if (it->second.ref->has_link_to(targets_.render)) {
    return;
  }

  it->second.ref->Unlink();

  if (!targets_.render) {
    FX_LOGS(WARNING) << "Tried to route AudioRenderer, but no output for the given usage exist.";
    return;
  }

  AudioObject::LinkObjects(it->second.ref, fbl::RefPtr(targets_.render));
}

void RouteGraph::RemoveRenderer(AudioObject* renderer) {
  FX_DCHECK(renderer->is_audio_renderer());
  AUD_VLOG(TRACE) << "Removing renderer from route graph: " << renderer;

  renderer->Unlink();
  renderers_.erase(renderer);
}

void RouteGraph::AddCapturer(fbl::RefPtr<AudioObject> capturer) {
  FX_DCHECK(capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding capturer to route graph: " << capturer.get();

  capturers_.insert({capturer.get(), RoutableOwnedObject{capturer, {}}});
}

void RouteGraph::SetCapturerRoutingProfile(AudioObject* capturer, RoutingProfile profile) {
  FX_DCHECK(capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting capturer route profile: " << capturer;

  auto it = capturers_.find(capturer);
  if (it == capturers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_capture_usage()) {
    it->second.ref->Unlink();
    return;
  }

  if (it->second.ref->has_link_to(targets_.capture)) {
    return;
  }

  it->second.ref->Unlink();

  if (!targets_.capture) {
    FX_LOGS(WARNING) << "Tried to route AudioCapturer, but no inputs exist.";
    return;
  }

  AudioObject::LinkObjects(fbl::RefPtr(targets_.capture), it->second.ref);
}

void RouteGraph::RemoveCapturer(AudioObject* capturer) {
  FX_DCHECK(capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Removing capturer from route graph: " << capturer;

  capturer->Unlink();
  capturers_.erase(capturer);
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::AddLoopbackCapturer(fbl::RefPtr<AudioObject> loopback_capturer) {
  FX_DCHECK(loopback_capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding loopback capturer to route graph: " << loopback_capturer.get();

  loopback_capturers_.insert({loopback_capturer.get(), RoutableOwnedObject{loopback_capturer, {}}});
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::SetLoopbackCapturerRoutingProfile(AudioObject* loopback_capturer,
                                                   RoutingProfile profile) {
  FX_DCHECK(loopback_capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting loopback capturer route profile: " << loopback_capturer;

  auto it = loopback_capturers_.find(loopback_capturer);
  if (it == loopback_capturers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_capture_usage()) {
    it->second.ref->Unlink();
    return;
  }

  if (it->second.ref->has_link_to(targets_.loopback)) {
    return;
  }

  it->second.ref->Unlink();

  if (!targets_.loopback) {
    FX_LOGS(WARNING) << "Tried to route loopback AudioCapturer, but no outputs exist.";
    return;
  }

  AudioObject::LinkObjects(fbl::RefPtr(targets_.loopback), it->second.ref);
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::RemoveLoopbackCapturer(AudioObject* loopback_capturer) {
  FX_DCHECK(loopback_capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting loopback capturer from route graph: " << loopback_capturer;

  loopback_capturer->Unlink();
  loopback_capturers_.erase(loopback_capturer);
}

void RouteGraph::LinkRenderersTo(AudioDevice* output) {
  std::for_each(renderers_.begin(), renderers_.end(), [output](auto& renderer_pair) {
    auto& [_, renderer] = renderer_pair;
    if (!renderer.profile.routable || !renderer.profile.usage.is_render_usage()) {
      return;
    }

    FX_CHECK(renderer.ref->dest_link_count() == 0);
    AudioObject::LinkObjects(renderer.ref, fbl::RefPtr(output));
  });
}

void RouteGraph::LinkCapturersTo(AudioDevice* input) {
  std::for_each(capturers_.begin(), capturers_.end(), [input](auto& capturer_pair) {
    auto& [_, capturer] = capturer_pair;
    if (!capturer.profile.routable || !capturer.profile.usage.is_capture_usage()) {
      return;
    }

    FX_CHECK(capturer.ref->source_link_count() == 0);
    AudioObject::LinkObjects(fbl::RefPtr(input), capturer.ref);
  });
}

void RouteGraph::LinkLoopbackCapturersTo(AudioDevice* output) {
  std::for_each(loopback_capturers_.begin(), loopback_capturers_.end(),
                [output](auto& loopback_capturer_pair) {
                  auto& [_, loopback_capturer] = loopback_capturer_pair;
                  if (!loopback_capturer.profile.routable ||
                      !loopback_capturer.profile.usage.is_capture_usage()) {
                    return;
                  }

                  FX_CHECK(loopback_capturer.ref->source_link_count() == 0);
                  AudioObject::LinkObjects(fbl::RefPtr(output), loopback_capturer.ref);
                });
}

void RouteGraph::UpdateGraphForDevicesChange() {
  auto [targets, unlink_command] = CalculateTargets();
  targets_ = targets;
  Unlink(unlink_command);

  if (unlink_command.renderers && targets_.render) {
    LinkRenderersTo(targets_.render);
  }

  if (unlink_command.loopback_capturers && targets_.loopback) {
    LinkLoopbackCapturersTo(targets_.loopback);
  }

  if (unlink_command.capturers && targets_.capture) {
    LinkCapturersTo(targets_.capture);
  }
}

std::pair<RouteGraph::Targets, RouteGraph::UnlinkCommand> RouteGraph::CalculateTargets() const {
  auto new_render_target = outputs_.empty() ? nullptr : outputs_.front();
  auto new_loopback_target = [this]() {
    auto it = std::find_if(outputs_.begin(), outputs_.end(),
                           [this](auto output) { return output != throttle_output_.get(); });

    return it == outputs_.end() ? nullptr : *it;
  }();
  auto new_capture_target = inputs_.empty() ? nullptr : inputs_.front();

  return {Targets{.render = new_render_target,
                  .loopback = new_loopback_target,
                  .capture = new_capture_target},
          UnlinkCommand{.renderers = new_render_target != targets_.render,
                        .loopback_capturers = new_loopback_target != targets_.loopback,
                        .capturers = new_capture_target != targets_.capture}};
}

void RouteGraph::Unlink(UnlinkCommand unlink_command) {
  if (unlink_command.loopback_capturers) {
    std::for_each(loopback_capturers_.begin(), loopback_capturers_.end(),
                  [](auto& loopback_capturer) { loopback_capturer.second.ref->Unlink(); });
  }

  if (unlink_command.renderers) {
    std::for_each(renderers_.begin(), renderers_.end(),
                  [](auto& renderer) { renderer.second.ref->Unlink(); });
  }

  if (unlink_command.capturers) {
    std::for_each(capturers_.begin(), capturers_.end(),
                  [](auto& capturer) { capturer.second.ref->Unlink(); });
  }
}

}  // namespace media::audio
