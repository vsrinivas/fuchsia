// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "services/media/audio/audio_output.h"
#include "services/media/audio/audio_output_manager.h"
#include "services/media/audio/audio_server_impl.h"
#include "services/media/audio/audio_track_to_output_link.h"
#include "services/media/audio/platform/generic/throttle_output.h"

namespace mojo {
namespace media {
namespace audio {

static constexpr size_t  THREAD_POOL_SZ = 2;
static const std::string THREAD_PREFIX("AudioMixer");

// TODO(johngro): This needs to be replaced with a proper HAL
extern AudioOutputPtr CreateDefaultAlsaOutput(AudioOutputManager* manager);

AudioOutputManager::AudioOutputManager(AudioServerImpl* server)
  : server_(server) {
}

AudioOutputManager::~AudioOutputManager() {
  Shutdown();
  DCHECK_EQ(outputs_.size(), 0u);
  DCHECK(!thread_pool_);
}

MediaResult AudioOutputManager::Init() {
  // Step #1: Initialize the mixing thread pool.
  //
  // TODO(johngro): make the thread pool size proportional to the maximum
  // number of cores available in the system.
  //
  // TODO(johngro): make sure that the threads are executed at an elevated
  // priority, not the default priority.
  thread_pool_ = new base::SequencedWorkerPool(THREAD_POOL_SZ, THREAD_PREFIX);

  // Step #2: Instantiate all of the built-in audio output devices.
  //
  // TODO(johngro): Come up with a better way of doing this based on our
  // platform.  Right now, we just create some hardcoded default outputs and
  // leave it at that.
  outputs_.emplace(audio::ThrottleOutput::New(this));
  {
    AudioOutputPtr alsa = CreateDefaultAlsaOutput(this);
    if (alsa) { outputs_.emplace(alsa); }
  }

  // Step #3: Being monitoring for plug/unplug events for pluggable audio
  // output devices.
  //
  // TODO(johngro): Implement step #3.  Right now, the details are behind
  // hot-plug monitoring are TBD, so the feature is not implemented.

  // Step #4: Attempt to initialize each of the audio outputs we have created,
  // then kick off the callback engine for each of them.
  for (auto iter = outputs_.begin(); iter != outputs_.end(); ) {
    const AudioOutputPtr& output = *iter;
    auto tmp = iter++;
    DCHECK(output);

    // Create a sequenced task runner for this output.  It will be used by the
    // output to schedule jobs (such as mixing) on the thread pool.
    scoped_refptr<base::SequencedTaskRunner> task_runner =
      thread_pool_->GetSequencedTaskRunnerWithShutdownBehavior(
        thread_pool_->GetSequenceToken(),
        base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);

    MediaResult res = output->Init(output, task_runner);
    if (res != MediaResult::OK) {
      // TODO(johngro): Probably should log something about this, assuming that
      // the output has not already.
      outputs_.erase(tmp);
    }
  }

  return MediaResult::OK;
}

void AudioOutputManager::Shutdown() {
  // Are we already shutdown (or were we never successfully initialized?)
  if (thread_pool_ == nullptr) {
    DCHECK_EQ(outputs_.size(), 0u);
    return;
  }

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

  // Step #3: Shutdown and release our thread pool.  Since we have shut down all
  // of our outputs, any pending tasks left in the task runner are now no-ops,
  // so it does not matter that the task runner is going to cancel them all
  // (instead of blocking) when we shut it down.
  thread_pool_->Shutdown();
  thread_pool_ = nullptr;
}

void AudioOutputManager::ShutdownOutput(AudioOutputPtr output) {
  // No one should be calling this method if we have been shut down (or never
  // successfully started).
  DCHECK(thread_pool_);

  auto iter = outputs_.find(output);
  if (iter != outputs_.end()) {
    output->Shutdown();
    outputs_.erase(iter);
  }
}

void AudioOutputManager::SelectOutputsForTrack(AudioTrackImplPtr track) {
  // TODO(johngro): Someday, base this on policy.  For now, every track gets
  // assigned to every output in the system.
  DCHECK(track);

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  for (auto output : outputs_) {
    auto link = AudioTrackToOutputLink::New(track, output);
    DCHECK(output);
    DCHECK(link);

    // If we cannot add this link to the output, it's because the output is in
    // the process of shutting down (we didn't want to hang out with that guy
    // anyway)
    if (output->AddTrackLink(link) == MediaResult::OK) {
      track->AddOutput(link);
    }
  }
}

void AudioOutputManager::ScheduleMessageLoopTask(
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  DCHECK(server_);
  server_->ScheduleMessageLoopTask(from_here, task);
}

}  // namespace audio
}  // namespace media
}  // namespace mojo
