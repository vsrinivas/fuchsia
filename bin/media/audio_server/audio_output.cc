// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_output.h"

#include "garnet/bin/media/audio_server/audio_output_manager.h"
#include "garnet/bin/media/audio_server/audio_renderer_to_output_link.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace media {
namespace audio {

AudioOutput::AudioOutput(AudioOutputManager* manager)
    : manager_(manager), db_gain_(0.0f) {
  FXL_DCHECK(manager_);
  // TODO(johngro) : See MG-940.  Eliminate this priority boost as soon as we
  // have a more official way of meeting real-time latency requirements.
  mix_domain_ = ::audio::dispatcher::ExecutionDomain::Create(24);
  mix_wakeup_ = ::audio::dispatcher::WakeupEvent::Create();
}

AudioOutput::~AudioOutput() {
  FXL_DCHECK(shutting_down());
}

MediaResult AudioOutput::AddRendererLink(AudioRendererToOutputLinkPtr link) {
  MediaResult res = InitializeLink(link);

  if (res == MediaResult::OK) {
    fxl::MutexLocker locker(&mutex_);

    // Assert that we are the output in this link.
    FXL_DCHECK(this == link->GetOutput().get());

    if (shutting_down()) {
      return MediaResult::SHUTTING_DOWN;
    }

    auto insert_result = links_.emplace(link);
    FXL_DCHECK(insert_result.second);
  } else {
    // TODO(johngro): Output didn't like this renderer for some reason... Should
    // probably log something about this.
  }

  return res;
}

MediaResult AudioOutput::RemoveRendererLink(
    const AudioRendererToOutputLinkPtr& link) {
  fxl::MutexLocker locker(&mutex_);

  if (shutting_down()) {
    return MediaResult::SHUTTING_DOWN;
  }

  auto iter = links_.find(link);
  if (iter == links_.end()) {
    return MediaResult::NOT_FOUND;
  }

  links_.erase(iter);
  return MediaResult::OK;
}

void AudioOutput::Wakeup() {
  FXL_DCHECK(mix_wakeup_ != nullptr);
  mix_wakeup_->Signal();
}

MediaResult AudioOutput::Init() {
  if ((mix_domain_ == nullptr) || (mix_wakeup_ == nullptr)) {
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  // clang-format off
  ::audio::dispatcher::WakeupEvent::ProcessHandler process_handler(
      [ output = fbl::WrapRefPtr(this) ]
      (::audio::dispatcher::WakeupEvent * event) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
        output->OnWakeup();
        return ZX_OK;
      });
  // clang-format on

  zx_status_t res =
      mix_wakeup_->Activate(mix_domain_, fbl::move(process_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate wakeup event for AudioOutput!  "
                   << "(res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  return MediaResult::OK;
}

void AudioOutput::Cleanup() {}

MediaResult AudioOutput::InitializeLink(
    const AudioRendererToOutputLinkPtr& link) {
  FXL_DCHECK(link);
  return MediaResult::OK;
}

void AudioOutput::ShutdownSelf() {
  // If we are not already in the process of shutting down, send a message to
  // the main message loop telling it to complete the shutdown process.
  if (!shutting_down()) {
    FXL_DCHECK(mix_domain_);
    mix_domain_->DeactivateFromWithinDomain();

    // clang-format off
    FXL_DCHECK(manager_);
    manager_->ScheduleMessageLoopTask(
      [ manager = manager_, self = fbl::WrapRefPtr(this) ]() {
        manager->ShutdownOutput(self);
      });
    // clang-format on
  }
}

void AudioOutput::DeactivateDomain() {
  if (mix_domain_ != nullptr) {
    mix_domain_->Deactivate();
  }
}

MediaResult AudioOutput::Startup() {
  // If our derived class failed to initialize, Just get out.  We are being
  // called by the output manager, and they will remove us from the set of
  // active outputs as a result of us failing to initialize.
  MediaResult res = Init();
  if (res != MediaResult::OK) {
    DeactivateDomain();
    return res;
  }

  // Poke the output once so it gets a chance to actually start running.
  Wakeup();

  return MediaResult::OK;
}

void AudioOutput::Shutdown() {
  if (shut_down_) {
    return;
  }

  // Make sure no new callbacks can be generated, and that pending callbacks
  // have been nerfed.
  DeactivateDomain();

  // Unlink ourselves from all of our renderers.
  UnlinkFromRenderers();

  // Give our derived class a chance to clean up its resources.
  Cleanup();

  // We are now completely shut down.  The only reason we have this flag is to
  // make sure that Shutdown is idempotent.
  shut_down_ = true;
}

void AudioOutput::UnlinkFromRenderers() {
  AudioRendererToOutputLinkSet old_links;

  {
    fxl::MutexLocker locker(&mutex_);
    old_links.swap(links_);
  }

  for (const auto& link : old_links) {
    FXL_DCHECK(link);
    AudioRendererImplPtr renderer = link->GetRenderer();
    if (renderer) {
      renderer->RemoveOutput(link);
    }
  }
}

bool AudioOutput::UpdatePlugState(bool plugged, zx_time_t plug_time) {
  if ((plugged != plugged_) && (plug_time >= plug_time_)) {
    plugged_ = plugged;
    plug_time_ = plug_time;
    return true;
  }

  return false;
}

}  // namespace audio
}  // namespace media
