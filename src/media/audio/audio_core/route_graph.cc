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

RouteGraph::RouteGraph(const RoutingConfig& routing_config, LinkMatrix* link_matrix)
    : link_matrix_(*link_matrix), routing_config_(routing_config) {
  FX_DCHECK(link_matrix);

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
                                   std::shared_ptr<AudioOutput> throttle_output) {
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

  renderers_.insert(
      {renderer.get(), RoutableOwnedObject{std::shared_ptr<AudioObject>(renderer.release()), {}}});
}

void RouteGraph::SetRendererRoutingProfile(const AudioObject& renderer, RoutingProfile profile) {
  FX_DCHECK(renderer.is_audio_renderer());
  FX_DCHECK(renderer.format_valid() || !profile.routable)
      << "AudioRenderer without PCM format was added to route graph";
  AUD_VLOG(TRACE) << "Setting renderer route profile: " << &renderer;

  auto it = renderers_.find(&renderer);
  if (it == renderers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_render_usage()) {
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  auto output = OutputForUsage(it->second.profile.usage);
  if (!output.is_linkable()) {
    FX_LOGS(WARNING) << "Tried to route AudioRenderer, but no output for the given usage exist.";
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (link_matrix_.AreLinked(*it->second.ref, *output.device)) {
    FX_LOGS(WARNING) << "renderer already linked";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  link_matrix_.LinkObjects(it->second.ref, output.device->shared_from_this(), output.transform);
}

void RouteGraph::RemoveRenderer(const AudioObject& renderer) {
  FX_DCHECK(renderer.is_audio_renderer());
  AUD_VLOG(TRACE) << "Removing renderer from route graph: " << &renderer;

  auto it = renderers_.find(&renderer);
  if (it == renderers_.end()) {
    AUD_VLOG(TRACE) << "Renderer " << &renderer << " was not present in graph.";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);
  renderers_.erase(it);
}

void RouteGraph::AddCapturer(std::unique_ptr<AudioObject> capturer) {
  FX_DCHECK(capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding capturer to route graph: " << capturer.get();

  capturers_.insert(
      {capturer.get(), RoutableOwnedObject{std::shared_ptr<AudioObject>(capturer.release()), {}}});
}

void RouteGraph::SetCapturerRoutingProfile(const AudioObject& capturer, RoutingProfile profile) {
  FX_DCHECK(capturer.is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting capturer route profile: " << &capturer;

  auto it = capturers_.find(&capturer);
  if (it == capturers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_capture_usage()) {
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (!targets_.capture.is_linkable()) {
    FX_LOGS(WARNING) << "Tried to route AudioCapturer, but no inputs exist.";
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (link_matrix_.AreLinked(*it->second.ref, *targets_.capture.device)) {
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  link_matrix_.LinkObjects(targets_.capture.device->shared_from_this(), it->second.ref,
                           targets_.capture.transform);
}

void RouteGraph::RemoveCapturer(const AudioObject& capturer) {
  FX_DCHECK(capturer.is_audio_capturer());
  AUD_VLOG(TRACE) << "Removing capturer from route graph: " << &capturer;

  auto it = capturers_.find(&capturer);
  if (it == capturers_.end()) {
    AUD_VLOG(TRACE) << "Capturer " << &capturer << " was not present in graph.";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);
  capturers_.erase(it);
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::AddLoopbackCapturer(std::unique_ptr<AudioObject> loopback_capturer) {
  FX_DCHECK(loopback_capturer->is_audio_capturer());
  AUD_VLOG(TRACE) << "Adding loopback capturer to route graph: " << loopback_capturer.get();

  loopback_capturers_.insert(
      {loopback_capturer.get(),
       RoutableOwnedObject{std::shared_ptr<AudioObject>(loopback_capturer.release()), {}}});
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::SetLoopbackCapturerRoutingProfile(const AudioObject& loopback_capturer,
                                                   RoutingProfile profile) {
  FX_DCHECK(loopback_capturer.is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting loopback capturer route profile: " << &loopback_capturer;

  auto it = loopback_capturers_.find(&loopback_capturer);
  if (it == loopback_capturers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered renderer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_capture_usage()) {
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (!targets_.loopback.is_linkable()) {
    FX_LOGS(WARNING) << "Tried to route loopback AudioCapturer, but no outputs exist.";
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (link_matrix_.AreLinked(*it->second.ref, *targets_.loopback.device)) {
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  link_matrix_.LinkObjects(targets_.loopback.device->shared_from_this(), it->second.ref,
                           targets_.loopback.transform);
}

// TODO(39627): Only accept capturers of loopback type.
void RouteGraph::RemoveLoopbackCapturer(const AudioObject& loopback_capturer) {
  FX_DCHECK(loopback_capturer.is_audio_capturer());
  AUD_VLOG(TRACE) << "Setting loopback capturer from route graph: " << &loopback_capturer;

  auto it = loopback_capturers_.find(&loopback_capturer);
  if (it == loopback_capturers_.end()) {
    AUD_VLOG(TRACE) << "Loopback capturer " << &loopback_capturer << " was not present in graph.";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);
  loopback_capturers_.erase(it);
}

void RouteGraph::UpdateGraphForDeviceChange() {
  auto [targets, unlink_command] = CalculateTargets();
  targets_ = targets;
  Unlink(unlink_command);

  if (std::any_of(unlink_command.renderers.begin(), unlink_command.renderers.end(),
                  [](auto unlink) { return unlink; })) {
    std::for_each(renderers_.begin(), renderers_.end(), [this](auto& renderer) {
      Target output;
      if (!renderer.second.profile.routable ||
          !((output = OutputForUsage(renderer.second.profile.usage)).is_linkable()) ||
          link_matrix_.DestLinkCount(*renderer.second.ref) > 0u) {
        return;
      }

      link_matrix_.LinkObjects(renderer.second.ref, output.device->shared_from_this(),
                               output.transform);
    });
  }

  if (unlink_command.loopback_capturers && targets_.loopback.is_linkable()) {
    std::for_each(loopback_capturers_.begin(), loopback_capturers_.end(),
                  [this, target = targets_.loopback](auto& loopback_capturer) {
                    if (!loopback_capturer.second.profile.routable ||
                        !loopback_capturer.second.profile.usage.is_capture_usage()) {
                      return;
                    }

                    FX_DCHECK(link_matrix_.SourceLinkCount(*loopback_capturer.second.ref) == 0);
                    link_matrix_.LinkObjects(target.device->shared_from_this(),
                                             loopback_capturer.second.ref, target.transform);
                  });
  }

  if (unlink_command.capturers && targets_.capture.is_linkable()) {
    std::for_each(capturers_.begin(), capturers_.end(),
                  [this, target = targets_.capture](auto& capturer) {
                    if (!capturer.second.profile.routable ||
                        !capturer.second.profile.usage.is_capture_usage()) {
                      return;
                    }

                    FX_DCHECK(link_matrix_.SourceLinkCount(*capturer.second.ref) == 0);
                    link_matrix_.LinkObjects(target.device->shared_from_this(), capturer.second.ref,
                                             target.transform);
                  });
  }
}

std::pair<RouteGraph::Targets, RouteGraph::UnlinkCommand> RouteGraph::CalculateTargets() const {
  // We generate a new set of targets.
  // We generate an unlink command to unlink anything linked to a target which has changed.

  std::array<Target, fuchsia::media::RENDER_USAGE_COUNT> new_render_targets = {};
  std::array<bool, fuchsia::media::RENDER_USAGE_COUNT> unlink_renderers = {};
  for (const auto& usage : kRenderUsages) {
    const auto idx = fidl::ToUnderlying(usage);
    new_render_targets[idx] = [this, usage]() {
      for (auto output : outputs_) {
        if (output == throttle_output_.get()) {
          continue;
        }

        const auto& profile = DeviceProfile(output);
        if (profile.supports_usage(usage)) {
          return Target(output, profile.loudness_transform());
        }
      }

      return Target(throttle_output_.get(),
                    DeviceProfile(throttle_output_.get()).loudness_transform());
    }();

    unlink_renderers[idx] = targets_.render[idx].device != new_render_targets[idx].device;
  }

  auto new_loopback_target = [this]() {
    for (auto output : outputs_) {
      if (output == throttle_output_.get()) {
        continue;
      }

      const auto& profile = DeviceProfile(output);
      if (profile.eligible_for_loopback()) {
        return Target(output, profile.loudness_transform());
      }
    }

    return Target{};
  }();

  auto new_capture_device = inputs_.empty() ? nullptr : inputs_.front();
  auto new_capture_target_transform =
      new_capture_device ? DeviceProfile(new_capture_device).loudness_transform() : nullptr;
  auto new_capture_target = Target(new_capture_device, new_capture_target_transform);

  return {
      Targets{.render = new_render_targets,
              .loopback = new_loopback_target,
              .capture = new_capture_target},
      UnlinkCommand{.renderers = unlink_renderers,
                    .loopback_capturers = new_loopback_target.device != targets_.loopback.device,
                    .capturers = new_capture_target.device != targets_.capture.device}};
}

void RouteGraph::Unlink(UnlinkCommand unlink_command) {
  std::for_each(renderers_.begin(), renderers_.end(), [this, &unlink_command](auto& renderer) {
    if (renderer.second.profile.usage.is_render_usage() &&
        unlink_command
            .renderers[fidl::ToUnderlying(renderer.second.profile.usage.render_usage())]) {
      link_matrix_.Unlink(*renderer.second.ref);
    }
  });

  if (unlink_command.loopback_capturers) {
    std::for_each(
        loopback_capturers_.begin(), loopback_capturers_.end(),
        [this](auto& loopback_capturer) { link_matrix_.Unlink(*loopback_capturer.second.ref); });
  }

  if (unlink_command.capturers) {
    std::for_each(capturers_.begin(), capturers_.end(),
                  [this](auto& capturer) { link_matrix_.Unlink(*capturer.second.ref); });
  }
}

RouteGraph::Target RouteGraph::OutputForUsage(const fuchsia::media::Usage& usage) const {
  if (!usage.is_render_usage()) {
    return Target();
  }

  auto idx = fidl::ToUnderlying(usage.render_usage());
  return targets_.render[idx];
}

const RoutingConfig::DeviceProfile& RouteGraph::DeviceProfile(AudioDevice* device) const {
  auto driver = device->driver();
  if (!driver) {
    return routing_config_.default_device_profile();
  }

  const auto& device_id = driver->persistent_unique_id();
  return routing_config_.device_profile(device_id);
}

}  // namespace media::audio
