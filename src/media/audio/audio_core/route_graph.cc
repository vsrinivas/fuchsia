// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/route_graph.h"

#include <algorithm>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

namespace {

std::array<fuchsia::media::AudioRenderUsage, fuchsia::media::RENDER_USAGE_COUNT> kRenderUsages = {
    fuchsia::media::AudioRenderUsage::BACKGROUND, fuchsia::media::AudioRenderUsage::MEDIA,
    fuchsia::media::AudioRenderUsage::INTERRUPTION, fuchsia::media::AudioRenderUsage::SYSTEM_AGENT,
    fuchsia::media::AudioRenderUsage::COMMUNICATION};

}  // namespace

RouteGraph::RouteGraph(const RoutingConfig& routing_config) : routing_config_(routing_config) {
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND) == 0);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA) == 1);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION) == 2);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT) == 3);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION) == 4);
  static_assert(fuchsia::media::RENDER_USAGE_COUNT == 5);

  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::BACKGROUND) == 0);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::FOREGROUND) == 1);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT) == 2);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::COMMUNICATION) == 3);
  static_assert(fuchsia::media::CAPTURE_USAGE_COUNT == 4);
}

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
  UpdateGraphForDeviceChange();
}

void RouteGraph::RemoveOutput(AudioDevice* output) {
  AUD_VLOG(TRACE) << "Removing output device from graph: " << output;

  auto it = std::find(outputs_.begin(), outputs_.end(), output);
  if (it == outputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered output device from the route graph.";
    return;
  }

  outputs_.erase(it);
  UpdateGraphForDeviceChange();
}

void RouteGraph::AddInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Added input device to route graph: " << input;

  inputs_.push_front(input);
  UpdateGraphForDeviceChange();
}

void RouteGraph::RemoveInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Removing input device to route graph: " << input;

  auto it = std::find(inputs_.begin(), inputs_.end(), input);
  if (it == inputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered input device from the route graph.";
    return;
  }

  inputs_.erase(it);
  UpdateGraphForDeviceChange();
}

void RouteGraph::AddRenderer(std::unique_ptr<AudioObject> renderer) {
  FX_DCHECK(throttle_output_);
  FX_DCHECK(renderer->is_audio_renderer());
  AUD_VLOG(TRACE) << "Adding renderer route graph: " << renderer.get();

  renderers_.insert({renderer.get(), RoutableOwnedObject{fbl::AdoptRef(renderer.release()), {}}});
}

void RouteGraph::SetRendererRoutingProfile(AudioObject* renderer, RoutingProfile profile) {
  FX_DCHECK(renderer->is_audio_renderer());
  FX_DCHECK(renderer->format_valid() || !profile.routable)
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

  AudioDevice* output = OutputForUsage(it->second.profile.usage);
  if (it->second.ref->has_link_to(output)) {
    return;
  }

  it->second.ref->Unlink();

  if (!output) {
    FX_LOGS(WARNING) << "Tried to route AudioRenderer, but no output for the given usage exist.";
    return;
  }

  AudioObject::LinkObjects(it->second.ref, fbl::RefPtr(output));
}

void RouteGraph::RemoveRenderer(AudioObject* renderer) {
  FX_DCHECK(renderer->is_audio_renderer());
  AUD_VLOG(TRACE) << "Removing renderer from route graph: " << renderer;

  renderer->Unlink();
  renderers_.erase(renderer);
}

void RouteGraph::AddCapturer(std::unique_ptr<AudioObject> capturer) {
  FX_DCHECK(capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding capturer to route graph: " << capturer.get();

  capturers_.insert({capturer.get(), RoutableOwnedObject{fbl::AdoptRef(capturer.release()), {}}});
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
void RouteGraph::AddLoopbackCapturer(std::unique_ptr<AudioObject> loopback_capturer) {
  FX_DCHECK(loopback_capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding loopback capturer to route graph: " << loopback_capturer.get();

  loopback_capturers_.insert({loopback_capturer.get(),
                              RoutableOwnedObject{fbl::AdoptRef(loopback_capturer.release()), {}}});
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

void RouteGraph::UpdateGraphForDeviceChange() {
  auto [targets, unlink_command] = CalculateTargets();
  targets_ = targets;
  Unlink(unlink_command);

  if (std::any_of(unlink_command.renderers.begin(), unlink_command.renderers.end(),
                  [](auto unlink) { return unlink; })) {
    std::for_each(renderers_.begin(), renderers_.end(), [this](auto& renderer) {
      AudioDevice* output;
      if (!renderer.second.profile.routable ||
          !(output = OutputForUsage(renderer.second.profile.usage)) ||
          renderer.second.ref->dest_link_count() > 0) {
        return;
      }

      AudioObject::LinkObjects(renderer.second.ref, fbl::RefPtr(output));
    });
  }

  if (unlink_command.loopback_capturers && targets_.loopback) {
    std::for_each(loopback_capturers_.begin(), loopback_capturers_.end(),
                  [target = targets_.loopback](auto& loopback_capturer) {
                    if (!loopback_capturer.second.profile.routable ||
                        !loopback_capturer.second.profile.usage.is_capture_usage()) {
                      return;
                    }

                    FX_DCHECK(loopback_capturer.second.ref->source_link_count() == 0);
                    AudioObject::LinkObjects(fbl::RefPtr(target), loopback_capturer.second.ref);
                  });
  }

  if (unlink_command.capturers && targets_.capture) {
    std::for_each(capturers_.begin(), capturers_.end(),
                  [target = targets_.capture](auto& capturer) {
                    if (!capturer.second.profile.routable ||
                        !capturer.second.profile.usage.is_capture_usage()) {
                      return;
                    }

                    FX_DCHECK(capturer.second.ref->source_link_count() == 0);
                    AudioObject::LinkObjects(fbl::RefPtr(target), capturer.second.ref);
                  });
  }
}

std::pair<RouteGraph::Targets, RouteGraph::UnlinkCommand> RouteGraph::CalculateTargets() const {
  // We generate a new set of targets.
  // We generate an unlink command to unlink anything linked to a target which has changed.

  std::array<AudioDevice*, fuchsia::media::RENDER_USAGE_COUNT> new_render_targets = {};
  std::array<bool, fuchsia::media::RENDER_USAGE_COUNT> unlink_renderers = {};
  for (const auto& usage : kRenderUsages) {
    const auto idx = fidl::ToUnderlying(usage);

    auto it = std::find_if(outputs_.begin(), outputs_.end(), [&usage, this](auto output) {
      if (output == throttle_output_.get()) {
        return false;
      }

      const auto& device_id = output->driver()->persistent_unique_id();
      auto device_profile = routing_config_.device_profile(device_id);
      return device_profile.supports_usage(usage);
    });
    if (it != outputs_.end()) {
      new_render_targets[idx] = *it;
    } else {
      new_render_targets[idx] = throttle_output_.get();
    }

    unlink_renderers[idx] = new_render_targets[idx];
  }

  auto new_loopback_target = [this]() {
    auto it = std::find_if(outputs_.begin(), outputs_.end(), [this](auto output) {
      if (output == throttle_output_.get()) {
        return false;
      }
      const auto& device_id = output->driver()->persistent_unique_id();
      auto device_profile = routing_config_.device_profile(device_id);
      return device_profile.eligible_for_loopback();
    });

    return it == outputs_.end() ? nullptr : *it;
  }();

  auto new_capture_target = inputs_.empty() ? nullptr : inputs_.front();

  return {Targets{.render = new_render_targets,
                  .loopback = new_loopback_target,
                  .capture = new_capture_target},
          UnlinkCommand{.renderers = unlink_renderers,
                        .loopback_capturers = new_loopback_target != targets_.loopback,
                        .capturers = new_capture_target != targets_.capture}};
}

void RouteGraph::Unlink(UnlinkCommand unlink_command) {
  std::for_each(renderers_.begin(), renderers_.end(), [&unlink_command](auto& renderer) {
    if (renderer.second.profile.usage.is_render_usage() &&
        unlink_command
            .renderers[fidl::ToUnderlying(renderer.second.profile.usage.render_usage())]) {
      renderer.second.ref->Unlink();
    }
  });

  if (unlink_command.loopback_capturers) {
    std::for_each(loopback_capturers_.begin(), loopback_capturers_.end(),
                  [](auto& loopback_capturer) { loopback_capturer.second.ref->Unlink(); });
  }

  if (unlink_command.capturers) {
    std::for_each(capturers_.begin(), capturers_.end(),
                  [](auto& capturer) { capturer.second.ref->Unlink(); });
  }
}

AudioDevice* RouteGraph::OutputForUsage(const fuchsia::media::Usage& usage) const {
  if (!usage.is_render_usage()) {
    return nullptr;
  }

  auto idx = fidl::ToUnderlying(usage.render_usage());
  return targets_.render[idx];
}

}  // namespace media::audio
