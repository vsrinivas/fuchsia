// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/audio_output_manager.h"

#include <string>

#include "apps/media/src/audio_server/audio_output.h"
#include "apps/media/src/audio_server/audio_server_impl.h"
#include "apps/media/src/audio_server/audio_track_to_output_link.h"
#include "apps/media/src/audio_server/platform/generic/throttle_output.h"

namespace mojo {
namespace media {
namespace audio {

// TODO(johngro): This needs to be replaced with a proper HAL
extern AudioOutputPtr CreateDefaultAlsaOutput(AudioOutputManager* manager);

AudioOutputManager::AudioOutputManager(AudioServerImpl* server)
    : server_(server) {}

AudioOutputManager::~AudioOutputManager() {
  Shutdown();
  FTL_DCHECK(outputs_.empty());
}

MediaResult AudioOutputManager::Init() {
  // Step #1: Instantiate all of the built-in audio output devices.
  //
  // TODO(johngro): Come up with a better way of doing this based on our
  // platform.  Right now, we just create some hardcoded default outputs and
  // leave it at that.
  outputs_.emplace(audio::ThrottleOutput::New(this));
  {
    AudioOutputPtr alsa = CreateDefaultAlsaOutput(this);
    if (alsa) {
      outputs_.emplace(alsa);
    }
  }

  // Step #2: Being monitoring for plug/unplug events for pluggable audio
  // output devices.
  //
  // TODO(johngro): Implement step #3.  Right now, the details are behind
  // hot-plug monitoring are TBD, so the feature is not implemented.

  // Step #3: Attempt to initialize each of the audio outputs we have created,
  // then kick off the callback engine for each of them.
  for (auto iter = outputs_.begin(); iter != outputs_.end();) {
    const AudioOutputPtr& output = *iter;
    auto tmp = iter++;
    FTL_DCHECK(output);

    MediaResult res = output->Init(output);
    if (res != MediaResult::OK) {
      // TODO(johngro): Probably should log something about this, assuming that
      // the output has not already.
      outputs_.erase(tmp);
    }
  }

  return MediaResult::OK;
}

void AudioOutputManager::Shutdown() {
  // Step #1: Stop monitoringing plug/unplug events.  We are shutting down and
  // no longer care about outputs coming and going.
  //
  // TODO(johngro): Implement step #1.  Right now, the details are behind
  // hot-plug monitoring are TBD, so the feature is not implemented.

  // Step #2: Shut down each currently active output in the system.  It is
  // possible for this to take a bit of time as outputs release their hardware,
  // but it should not take long.
  for (const auto& output_ptr : outputs_) {
    output_ptr->Shutdown();
  }
  outputs_.clear();
}

void AudioOutputManager::ShutdownOutput(AudioOutputPtr output) {
  auto iter = outputs_.find(output);
  if (iter != outputs_.end()) {
    output->Shutdown();
    outputs_.erase(iter);
  }
}

void AudioOutputManager::SelectOutputsForTrack(AudioTrackImplPtr track) {
  // TODO(johngro): Someday, base this on policy.  For now, every track gets
  // assigned to every output in the system.
  FTL_DCHECK(track);

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  for (auto output : outputs_) {
    auto link = AudioTrackToOutputLink::New(track, output);
    FTL_DCHECK(output);
    FTL_DCHECK(link);

    // If we cannot add this link to the output, it's because the output is in
    // the process of shutting down (we didn't want to hang out with that guy
    // anyway)
    if (output->AddTrackLink(link) == MediaResult::OK) {
      track->AddOutput(link);
    }
  }
}

void AudioOutputManager::ScheduleMessageLoopTask(const ftl::Closure& task) {
  FTL_DCHECK(server_);
  server_->ScheduleMessageLoopTask(task);
}

}  // namespace audio
}  // namespace media
}  // namespace mojo
