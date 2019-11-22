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

  if (!outputs_.empty()) {
    outputs_.front()->Unlink();
  }
  outputs_.push_front(output);
  LinkRenderersTo(output);
  LinkLoopbackCapturersTo(output);
}

void RouteGraph::RemoveOutput(AudioDevice* output) {
  AUD_VLOG(TRACE) << "Removing output device from graph: " << output;

  auto it = std::find(outputs_.begin(), outputs_.end(), output);
  if (it == outputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered output device from the route graph.";
    return;
  }

  const bool should_reroute = *it == outputs_.front();
  (*it)->Unlink();
  outputs_.erase(it);
  if (should_reroute && !outputs_.empty()) {
    LinkRenderersTo(outputs_.front());
    LinkLoopbackCapturersTo(outputs_.front());
  }
}

void RouteGraph::AddInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Added input device to route graph: " << input;

  if (!inputs_.empty()) {
    inputs_.front()->Unlink();
  }
  inputs_.push_front(input);
  LinkCapturersTo(input);
}

void RouteGraph::RemoveInput(AudioDevice* input) {
  AUD_VLOG(TRACE) << "Removing input device to route graph: " << input;

  auto it = std::find(inputs_.begin(), inputs_.end(), input);
  if (it == inputs_.end()) {
    FX_LOGS(WARNING) << "Attempted to remove unregistered input device from the route graph.";
    return;
  }

  const bool should_reroute = *it == inputs_.front();
  (*it)->Unlink();
  inputs_.erase(it);
  if (should_reroute && !inputs_.empty()) {
    LinkCapturersTo(inputs_.front());
  }
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

  const bool was_unrouted = !it->second.profile.routable;
  it->second.profile = std::move(profile);
  if (!it->second.profile.routable) {
    it->second.ref->Unlink();
  } else if (was_unrouted) {
    if (outputs_.empty()) {
      FX_LOGS(WARNING) << "Tried to route AudioRenderer, but no outputs exist.";
      return;
    }

    AudioObject::LinkObjects(it->second.ref, fbl::RefPtr(outputs_.front()));
  }
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

  const bool was_unrouted = !it->second.profile.routable;
  it->second.profile = std::move(profile);
  if (!it->second.profile.routable) {
    it->second.ref->Unlink();
  } else if (was_unrouted) {
    if (inputs_.empty()) {
      FX_LOGS(WARNING) << "Tried to route AudioCapturer, but no inputs exist.";
      return;
    }

    AudioObject::LinkObjects(fbl::RefPtr(inputs_.front()), it->second.ref);
  }
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

  const bool was_unrouted = !it->second.profile.routable;
  it->second.profile = std::move(profile);
  if (!it->second.profile.routable) {
    it->second.ref->Unlink();
  } else if (was_unrouted) {
    if (outputs_.empty() || outputs_.front() == throttle_output_.get()) {
      FX_LOGS(WARNING) << "Tried to route loopback AudioCapturer, but no outputs exist.";
      return;
    }

    AudioObject::LinkObjects(fbl::RefPtr(outputs_.front()), it->second.ref);
  }
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
    if (!renderer.profile.routable || renderer.ref->dest_link_count() > 0) {
      return;
    }

    AudioObject::LinkObjects(renderer.ref, fbl::RefPtr(output));
  });
}

void RouteGraph::LinkCapturersTo(AudioDevice* input) {
  std::for_each(capturers_.begin(), capturers_.end(), [input](auto& capturer_pair) {
    auto& [_, capturer] = capturer_pair;
    if (!capturer.profile.routable || capturer.ref->source_link_count() > 0) {
      return;
    }

    AudioObject::LinkObjects(fbl::RefPtr(input), capturer.ref);
  });
}

void RouteGraph::LinkLoopbackCapturersTo(AudioDevice* output) {
  // TODO(13339): Remove throttle_output_.
  if (output == throttle_output_.get()) {
    return;
  }

  std::for_each(
      loopback_capturers_.begin(), loopback_capturers_.end(),
      [output](auto& loopback_capturer_pair) {
        auto& [_, loopback_capturer] = loopback_capturer_pair;
        if (!loopback_capturer.profile.routable || loopback_capturer.ref->source_link_count() > 0) {
          return;
        }

        AudioObject::LinkObjects(fbl::RefPtr(output), loopback_capturer.ref);
      });
}

}  // namespace media::audio
