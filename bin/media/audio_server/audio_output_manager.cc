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

  MediaResult res = AddOutput(throttle_output);
  if (res != MediaResult::OK) {
    FTL_LOG(WARNING)
        << "AudioOutputManager failed to initalize the throttle output (res "
        << res << ")";
    return res;
  }

  throttle_output_ = throttle_output;

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

  // Step #2: Shut down each currently active output in the system.  It is
  // possible for this to take a bit of time as outputs release their hardware,
  // but it should not take long.
  for (const auto& output_ptr : outputs_) {
    output_ptr->Shutdown();
  }
  outputs_.clear();

  // TODO(johngro) : shut down the thread pool
}

MediaResult AudioOutputManager::AddOutput(AudioOutputPtr output) {
  FTL_DCHECK(output != nullptr);

  auto emplace_res = outputs_.emplace(output);
  FTL_DCHECK(emplace_res.second);

  MediaResult res = output->Init(output);
  if (res != MediaResult::OK) {
    outputs_.erase(emplace_res.first);
    output->Shutdown();
  }

  return res;
}

void AudioOutputManager::ShutdownOutput(AudioOutputPtr output) {
  auto iter = outputs_.find(output);
  if (iter != outputs_.end()) {
    output->Shutdown();
    outputs_.erase(iter);
  }
}

void AudioOutputManager::HandlePlugStateChange(AudioOutputPtr output,
                                               bool plugged,
                                               mx_time_t plug_time) {
  FTL_DCHECK(output);
  if (output->UpdatePlugState(plugged, plug_time)) {
    // TODO(johngro) : plugged/unpluged state has changed.  Time to re-evaluate
    // our audio routing policy.
  }
}

void AudioOutputManager::SelectOutputsForRenderer(
    AudioRendererImplPtr renderer) {
  FTL_DCHECK(renderer);

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  // All renderers get linked to the throttle output, no matter what.
  LinkOutputToRenderer(throttle_output_, renderer);

  switch (routing_policy_) {
    case RoutingPolicy::ALL_PLUGGED_OUTPUTS: {
      for (auto output : outputs_) {
        if ((output != throttle_output_) && output->plugged()) {
          LinkOutputToRenderer(output, renderer);
        }
      }
    } break;

    case RoutingPolicy::LAST_PLUGGED_OUTPUT: {
      AudioOutputPtr best_output = nullptr;

      for (auto output : outputs_) {
        if ((output != throttle_output_) && output->plugged() &&
            (!best_output || (best_output->plug_time() < output->plug_time()))) {
          best_output = output;
        }
      }

      if (best_output != nullptr) {
        LinkOutputToRenderer(best_output, renderer);
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

}  // namespace audio
}  // namespace media
