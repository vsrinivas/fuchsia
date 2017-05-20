// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_output_manager.h"

#include <string>

#include "apps/media/src/audio_server/audio_output.h"
#include "apps/media/src/audio_server/audio_plug_detector.h"
#include "apps/media/src/audio_server/audio_renderer_to_output_link.h"
#include "apps/media/src/audio_server/audio_server_impl.h"
#include "apps/media/src/audio_server/platform/generic/throttle_output.h"

namespace media {
namespace audio {

AudioOutputManager::AudioOutputManager(AudioServerImpl* server)
    : server_(server) {
  plug_detector_ = AudioPlugDetector::Create();
}

AudioOutputManager::~AudioOutputManager() {
  Shutdown();
  FTL_DCHECK(outputs_.empty());
}

MediaResult AudioOutputManager::Init() {
  // Step #1: Instantiate and initialize the default throttle output.
  auto throttle_output = ThrottleOutput::New(this);
  if (throttle_output == nullptr) {
    FTL_LOG(WARNING)
        << "AudioOutputManager failed to create default throttle output!";
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  MediaResult res = throttle_output->Init(throttle_output);
  if (res != MediaResult::OK) {
    FTL_LOG(WARNING)
        << "AudioOutputManager failed to initalize the throttle output (res "
        << res << ")";
    throttle_output->Shutdown();
  }
  throttle_output_ = std::move(throttle_output);

  // Step #2: Being monitoring for plug/unplug events for pluggable audio
  // output devices.
  FTL_DCHECK(plug_detector_ != nullptr);
  res = plug_detector_->Start(this);
  if (res != MediaResult::OK) {
    FTL_LOG(WARNING) << "AudioOutputManager failed to start plug detector (res "
                     << res << ")";
    return res;
  }

  return MediaResult::OK;
}

void AudioOutputManager::Shutdown() {
  // Step #1: Stop monitoringing plug/unplug events.  We are shutting down and
  // no longer care about outputs coming and going.
  FTL_DCHECK(plug_detector_ != nullptr);
  plug_detector_->Stop();

  // Step #2: Shutdown all of the active renderers in the system.
  while (!renderers_.empty()) {
    // Renderers remove themselves from the server's set of active renderers as
    // they shutdown.  Assert that the set's size is shrinking by one each time
    // we shut down a renderer so we know that we are making progress.
    size_t size_before = renderers_.size();
    (*renderers_.begin())->Shutdown();
    size_t size_after = renderers_.size();
    FTL_DCHECK(size_after < size_before);
  }

  // Step #3: Shut down each currently active output in the system.  It is
  // possible for this to take a bit of time as outputs release their hardware,
  // but it should not take long.
  for (const auto& output_ptr : outputs_) {
    output_ptr->Shutdown();
  }
  outputs_.clear();

  throttle_output_->Shutdown();
  throttle_output_ = nullptr;

  // TODO(johngro) : shut down the thread pool
}

void AudioOutputManager::AddRenderer(AudioRendererImplPtr renderer) {
  FTL_DCHECK(renderer);

  // Create a link between this renderer and the throttle output, assign it to
  // the renderer, and then add the renderer to the set of active renderers.
  auto link = AudioRendererToOutputLink::New(renderer, throttle_output_);
  FTL_DCHECK(link);

  if (throttle_output_->AddRendererLink(link) == MediaResult::OK) {
    renderer->SetThrottleOutput(link);
  }

  renderers_.insert(std::move(renderer));
}

MediaResult AudioOutputManager::AddOutput(AudioOutputPtr output) {
  FTL_DCHECK(output != nullptr);
  FTL_DCHECK(output != throttle_output_);

  auto emplace_res = outputs_.emplace(output);
  FTL_DCHECK(emplace_res.second);

  MediaResult res = output->Init(output);
  if (res != MediaResult::OK) {
    outputs_.erase(emplace_res.first);
    output->Shutdown();
  }

  if (output->plugged()) {
    OnOutputPlugged(output);
  }

  return res;
}

void AudioOutputManager::ShutdownOutput(AudioOutputPtr output) {
  FTL_DCHECK(output != nullptr);
  FTL_DCHECK(output != throttle_output_);

  auto iter = outputs_.find(output);
  if (iter != outputs_.end()) {
    if (output->UpdatePlugState(false, output->plug_time())) {
      OnOutputUnplugged(output);
    }
    output->Shutdown();
    outputs_.erase(iter);
  }
}

void AudioOutputManager::HandlePlugStateChange(AudioOutputPtr output,
                                               bool plugged,
                                               mx_time_t plug_time) {
  FTL_DCHECK(output);
  if (output->UpdatePlugState(plugged, plug_time)) {
    if (plugged) {
      OnOutputPlugged(output);
    } else {
      OnOutputUnplugged(output);
    }
  }
}

void AudioOutputManager::SelectOutputsForRenderer(
    AudioRendererImplPtr renderer) {
  FTL_DCHECK(renderer);

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  switch (routing_policy_) {
    case RoutingPolicy::ALL_PLUGGED_OUTPUTS: {
      for (auto output : outputs_) {
        if (output->plugged()) {
          LinkOutputToRenderer(output, renderer);
        }
      }
    } break;

    case RoutingPolicy::LAST_PLUGGED_OUTPUT: {
      AudioOutputPtr last_plugged = FindLastPluggedOutput();
      if (last_plugged != nullptr) {
        LinkOutputToRenderer(last_plugged, renderer);
      }

    } break;
  }
}

void AudioOutputManager::LinkOutputToRenderer(AudioOutputPtr output,
                                              AudioRendererImplPtr renderer) {
  FTL_DCHECK(output);
  FTL_DCHECK(renderer);

  auto link = AudioRendererToOutputLink::New(renderer, output);
  FTL_DCHECK(link);

  // If we cannot add this link to the output, it's because the output is in
  // the process of shutting down (we didn't want to hang out with that guy
  // anyway)
  if (output->AddRendererLink(link) == MediaResult::OK) {
    renderer->AddOutput(link);
  }
}

void AudioOutputManager::ScheduleMessageLoopTask(const ftl::Closure& task) {
  FTL_DCHECK(server_);
  server_->ScheduleMessageLoopTask(task);
}

AudioOutputPtr AudioOutputManager::FindLastPluggedOutput() {
  AudioOutputPtr best_output = nullptr;

  for (auto output : outputs_) {
    if (output->plugged() &&
       (!best_output || (best_output->plug_time() < output->plug_time()))) {
      best_output = output;
    }
  }

  return best_output;
}

void AudioOutputManager::OnOutputUnplugged(AudioOutputPtr output) {
  FTL_DCHECK(output && !output->plugged() && (output != throttle_output_));

  // This output was just unplugged.  Unlink it from all of its currently
  // linked renderers.  If we are applying 'last plugged' policy, replace it
  // with the new 'last plugged' output (if any)
  output->UnlinkFromRenderers();

  if (routing_policy_ == RoutingPolicy::LAST_PLUGGED_OUTPUT) {
    AudioOutputPtr replacement = FindLastPluggedOutput();
    if (replacement) {
      for (auto renderer : renderers_) {
        LinkOutputToRenderer(replacement, renderer);
      }
    }
  }
}

void AudioOutputManager::OnOutputPlugged(AudioOutputPtr output) {
  FTL_DCHECK(output && output->plugged() && (output != throttle_output_));

  switch (routing_policy_) {
      case RoutingPolicy::ALL_PLUGGED_OUTPUTS:
      // If we are following the 'all plugged outputs' routing policy, simply
      // add this newly plugged output to all of the active renderers.
      for (auto renderer : renderers_) {
        LinkOutputToRenderer(output, renderer);
      }
      break;

    case RoutingPolicy::LAST_PLUGGED_OUTPUT:
      // This output was just plugged in, deal with routing issues.  If we are
      // using the 'last plugged' policy, unlink all of the active renderers
      // from the outputs they are connected to, then link them to the newly
      // plugged output.
      //
      // Note; we need to make sure that this is actually the last plugged
      // output.  Because of the parallized nature of plug detection and stream
      // discovery, it is possible that two outputs might be plugged in at
      // similar times, but we handle their plugged status out-of-order.
      if (FindLastPluggedOutput() != output) return;

      for (const auto& unlink_tgt : outputs_) {
        if (unlink_tgt != output) {
          unlink_tgt->UnlinkFromRenderers();
        }
      }

      for (const auto& renderer : renderers_) {
        LinkOutputToRenderer(output, renderer);
      }
      break;
  }
}

}  // namespace audio
}  // namespace media
