// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/route_graph.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <sstream>

#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/logging_flags.h"

namespace media::audio {
namespace {

// TODO(fxbug.dev/55132): Remove this workaround. Just 64000 would still support the range needed.
static constexpr int32_t kMinUltrasoundRate = 96000;

bool DeviceConfigurationSupportsUsage(AudioDevice* device, StreamUsage usage) {
  if (usage != StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND) &&
      usage != StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)) {
    return true;
  }

  FX_DCHECK(device->format());
  auto device_rate = device->format()->frames_per_second();
  return device_rate >= kMinUltrasoundRate;
}

}  // namespace

RouteGraph::RouteGraph(LinkMatrix* link_matrix) : link_matrix_(*link_matrix) {
  FX_DCHECK(link_matrix);
}

RouteGraph::~RouteGraph() {
  if (throttle_release_fence_) {
    throttle_release_fence_->complete_ok();
  }
}

void RouteGraph::SetThrottleOutput(ThreadingModel* threading_model,
                                   std::shared_ptr<AudioOutput> throttle_output) {
  fpromise::bridge<void, void> bridge;
  threading_model->FidlDomain().ScheduleTask(bridge.consumer.promise().then(
      [throttle_output](fpromise::result<void, void>& _) { return throttle_output->Shutdown(); }));

  threading_model->FidlDomain().executor()->schedule_task(
      throttle_output->Startup().or_else([throttle_output](zx_status_t& error) {
        FX_PLOGS(ERROR, error) << "Failed to initialize the throttle output";
        return throttle_output->Shutdown();
      }));

  throttle_release_fence_ = {std::move(bridge.completer)};
  throttle_output_ = throttle_output;
  AddDeviceToRoutes(throttle_output_.get());
}

void RouteGraph::AddDeviceToRoutes(AudioDevice* device) {
  TRACE_DURATION("audio", "RouteGraph::AddDeviceToRoutes");

  // Add device, sorted with most-recently-plugged devices first. Use a stable sort so that ties are
  // broken by most-recently-added device, which helps make unit tests deterministic.
  devices_.push_front(device);
  std::stable_sort(devices_.begin(), devices_.end(), [](AudioDevice* a, AudioDevice* b) {
    if (a->plugged() != b->plugged()) {
      return a->plugged();
    }
    return a->plug_time() > b->plug_time();
  });

  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Added device " << device << " (" << (device->is_input() ? "input" : "output")
                  << ") to route graph";
    DisplayDevices();
  }
  UpdateGraphForDeviceChange();
}

void RouteGraph::RemoveDeviceFromRoutes(AudioDevice* device) {
  TRACE_DURATION("audio", "RouteGraph::RemoveDeviceFromRoutes");
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Removing device " << device << " ("
                  << (device->is_input() ? "input" : "output") << ") from route graph";
  }

  auto it = std::find(devices_.begin(), devices_.end(), device);
  if (it == devices_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered device from the route graph.";
    return;
  }

  // Unlink the device (don't just tell its sources/dests to Unlink) so LinkMatrix fully removes it.
  link_matrix_.Unlink(*device);

  devices_.erase(it);
  if constexpr (kLogRoutingChanges) {
    DisplayDevices();
  }
  UpdateGraphForDeviceChange();
}

bool RouteGraph::ContainsDevice(const AudioDevice* device) {
  return (std::find(devices_.begin(), devices_.end(), device) != devices_.end());
}

void RouteGraph::AddRenderer(std::shared_ptr<AudioObject> renderer) {
  TRACE_DURATION("audio", "RouteGraph::AddRenderer");
  FX_DCHECK(throttle_output_);
  FX_DCHECK(renderer->is_audio_renderer());
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Adding renderer " << renderer.get() << " (" << renderer->usage()->ToString()
                  << ") to route graph";
  }
  renderers_.insert({renderer.get(), RoutableOwnedObject{std::move(renderer), {}}});

  if constexpr (kLogRoutingChanges) {
    DisplayRenderers();
  }
}

void RouteGraph::SetRendererRoutingProfile(const AudioObject& renderer, RoutingProfile profile) {
  TRACE_DURATION("audio", "RouteGraph::SetRendererRoutingProfile");
  FX_DCHECK(renderer.is_audio_renderer());
  FX_LOGS(DEBUG) << "Setting renderer route profile: " << &renderer;

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

  auto output = TargetForUsage(it->second.profile.usage);
  if (!output.is_linkable()) {
    FX_LOGS(WARNING) << "Tried to route AudioRenderer, but no device available for usage "
                     << it->second.profile.usage.ToString();
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (link_matrix_.AreLinked(*it->second.ref, *output.device)) {
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  link_matrix_.LinkObjects(it->second.ref, output.device->shared_from_this(), output.transform);
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Setting renderer route profile: " << &renderer;
    DisplayRenderers();
    link_matrix_.DisplayCurrentRouting();
  }
}

void RouteGraph::RemoveRenderer(const AudioObject& renderer) {
  TRACE_DURATION("audio", "RouteGraph::RemoveRenderer");
  FX_DCHECK(renderer.is_audio_renderer());

  auto it = renderers_.find(&renderer);
  if (it == renderers_.end()) {
    FX_LOGS(INFO) << "Renderer " << &renderer << " was not present in graph.";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  renderers_.erase(it);
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Removed renderer from route graph: " << &renderer << " ("
                  << renderer.usage()->ToString() << ")";
    DisplayRenderers();
    link_matrix_.DisplayCurrentRouting();
  }
}

void RouteGraph::AddCapturer(std::shared_ptr<AudioObject> capturer) {
  TRACE_DURATION("audio", "RouteGraph::AddCapturer");
  FX_DCHECK(capturer->is_audio_capturer());

  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Adding capturer " << capturer.get() << " (" << capturer->usage()->ToString()
                  << ") to route graph";
  }

  capturers_.insert({capturer.get(), RoutableOwnedObject{std::move(capturer), {}}});

  if constexpr (kLogRoutingChanges) {
    DisplayCapturers();
  }
}

void RouteGraph::SetCapturerRoutingProfile(const AudioObject& capturer, RoutingProfile profile) {
  TRACE_DURATION("audio", "RouteGraph::SetCapturerRoutingProfile");
  FX_DCHECK(capturer.is_audio_capturer());
  FX_LOGS(DEBUG) << "Setting capturer route profile: " << &capturer;

  auto it = capturers_.find(&capturer);
  if (it == capturers_.end()) {
    FX_LOGS(WARNING) << "Tried to set routing policy for an unregistered capturer.";
    return;
  }

  it->second.profile = std::move(profile);
  if (!it->second.profile.routable || !it->second.profile.usage.is_capture_usage()) {
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  auto target = TargetForUsage(it->second.profile.usage);
  if (!target.is_linkable()) {
    FX_LOGS(WARNING) << "Tried to route AudioCapturer, but no device available for usage "
                     << it->second.profile.usage.ToString();
    link_matrix_.Unlink(*it->second.ref);
    return;
  }

  if (link_matrix_.AreLinked(*target.device, *it->second.ref)) {
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  link_matrix_.LinkObjects(target.device->shared_from_this(), it->second.ref, target.transform);
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Setting capturer route profile: " << &capturer;
    DisplayCapturers();
    link_matrix_.DisplayCurrentRouting();
  }
}

void RouteGraph::RemoveCapturer(const AudioObject& capturer) {
  TRACE_DURATION("audio", "RouteGraph::RemoveCapturer");
  FX_DCHECK(capturer.is_audio_capturer());

  auto it = capturers_.find(&capturer);
  if (it == capturers_.end()) {
    FX_LOGS(WARNING) << "Capturer " << &capturer << " was not present in graph.";
    return;
  }

  link_matrix_.Unlink(*it->second.ref);

  capturers_.erase(it);
  if constexpr (kLogRoutingChanges) {
    FX_LOGS(INFO) << "Removed capturer " << &capturer << " (" << capturer.usage()->ToString()
                  << ") from route graph";
    DisplayCapturers();
    link_matrix_.DisplayCurrentRouting();
  }
}

void RouteGraph::UpdateGraphForDeviceChange() {
  TRACE_DURATION("audio", "RouteGraph::UpdateGraphForDeviceChange");
  auto [targets, unlink_command] = CalculateTargets();
  targets_ = targets;
  Unlink(unlink_command);

  {
    TRACE_DURATION("audio", "RouteGraph::UpdateGraphForDeviceChange.renderers");
    std::for_each(renderers_.begin(), renderers_.end(), [this](auto& renderer) {
      Target target;
      if (!renderer.second.profile.routable ||
          !((target = TargetForUsage(renderer.second.profile.usage)).is_linkable()) ||
          link_matrix_.DestLinkCount(*renderer.second.ref) > 0u) {
        return;
      }

      link_matrix_.LinkObjects(renderer.second.ref, target.device->shared_from_this(),
                               target.transform);
    });
  }

  {
    TRACE_DURATION("audio", "RouteGraph::UpdateGraphForDeviceChange.capturers");
    std::for_each(capturers_.begin(), capturers_.end(), [this](auto& capturer) {
      Target target;
      if (!capturer.second.profile.routable ||
          !((target = TargetForUsage(capturer.second.profile.usage)).is_linkable()) ||
          link_matrix_.SourceLinkCount(*capturer.second.ref) > 0u) {
        return;
      }

      link_matrix_.LinkObjects(target.device->shared_from_this(), capturer.second.ref,
                               target.transform);
    });
  }
  if constexpr (kLogRoutingChanges) {
    DisplayRenderers();
    DisplayCapturers();
    DisplayDevices();
    link_matrix_.DisplayCurrentRouting();
  }
}

std::pair<RouteGraph::Targets, RouteGraph::UnlinkCommand> RouteGraph::CalculateTargets() const {
  TRACE_DURATION("audio", "RouteGraph::CalculateTargets");
  // We generate a new set of targets.
  // We generate an unlink command to unlink anything linked to a target which has changed.

  Targets new_targets = {};
  UnlinkCommand unlink;
  for (const auto& usage : kStreamUsages) {
    const auto idx = HashStreamUsage(usage);
    new_targets[idx] = [this, usage]() {
      for (auto device : devices_) {
        if (device == throttle_output_.get()) {
          continue;
        }

        if (device->profile().supports_usage(usage) &&
            DeviceConfigurationSupportsUsage(device, usage)) {
          return Target(device, device->profile().loudness_transform());
        }
      }

      if (usage.is_render_usage()) {
        return Target(throttle_output_.get(), throttle_output_->profile().loudness_transform());
      } else {
        return Target();
      }
    }();

    unlink[idx] = targets_[idx].device != new_targets[idx].device;
  }

  return {new_targets, unlink};
}

void RouteGraph::Unlink(UnlinkCommand unlink_command) {
  TRACE_DURATION("audio", "RouteGraph::Unlink");
  std::for_each(renderers_.begin(), renderers_.end(), [this, &unlink_command](auto& renderer) {
    auto usage = renderer.second.profile.usage;
    if (!usage.is_empty() && unlink_command[HashStreamUsage(usage)]) {
      link_matrix_.Unlink(*renderer.second.ref);
    }
  });
  std::for_each(capturers_.begin(), capturers_.end(), [this, &unlink_command](auto& capturer) {
    auto usage = capturer.second.profile.usage;
    if (!usage.is_empty() && unlink_command[HashStreamUsage(usage)]) {
      link_matrix_.Unlink(*capturer.second.ref);
    }
  });
}

RouteGraph::Target RouteGraph::TargetForUsage(const StreamUsage& usage) const {
  if (usage.is_empty()) {
    return Target();
  }
  return targets_[HashStreamUsage(usage)];
}

// The API is formed to return more than one output as the target for a RenderUsage, but the current
// audio_core implementation only routes to one output per usage.
std::unordered_set<AudioDevice*> RouteGraph::TargetsForRenderUsage(const RenderUsage& usage) {
  auto target = targets_[HashStreamUsage(StreamUsage::WithRenderUsage(usage))];
  if (!target.is_linkable()) {
    FX_LOGS(ERROR) << __FUNCTION__ << " (" << RenderUsageToString(usage)
                   << ") target is not linkable";
    return {};
  }

  if constexpr (kLogIdlePolicyCounts) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << RenderUsageToString(usage) << ") returning "
                  << target.device;
  }
  return {target.device};
}

std::shared_ptr<LoudnessTransform> RouteGraph::LoudnessTransformForUsage(const StreamUsage& usage) {
  return TargetForUsage(usage).transform;
}

void RouteGraph::DisplayRenderers() {
  std::stringstream stream;
  stream << "Renderers:";
  if (renderers_.empty()) {
    stream << " <empty>";
  } else {
    for (const auto& renderer : renderers_) {
      stream << " " << renderer.first;
    }
  }
  FX_LOGS(INFO) << stream.str();
}

void RouteGraph::DisplayCapturers() {
  std::stringstream stream;
  stream << "Capturers:";
  if (capturers_.empty()) {
    stream << " <empty>";
  } else {
    for (const auto& capturer : capturers_) {
      stream << " " << capturer.first;
    }
  }
  FX_LOGS(INFO) << stream.str();

  std::stringstream().swap(stream);
  stream << "Loopbacks:";
  if (loopback_capturers_.empty()) {
    stream << " <empty>";
  } else {
    for (const auto& capturer : loopback_capturers_) {
      stream << " " << capturer.first;
    }
  }
  FX_LOGS(INFO) << stream.str();
}

void RouteGraph::DisplayDevices() {
  std::stringstream stream;
  stream << "Devices:";
  for (const auto& device : devices_) {
    stream << " " << device;
  }
  FX_LOGS(INFO) << stream.str();
}

}  // namespace media::audio
