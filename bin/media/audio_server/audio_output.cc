// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_output.h"

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/bin/media/audio_server/audio_renderer_to_output_link.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace media {
namespace audio {

AudioOutput::AudioOutput(AudioDeviceManager* manager)
    : AudioDevice(Type::Output, manager), db_gain_(0.0f) {}

MediaResult AudioOutput::AddRendererLink(AudioRendererToOutputLinkPtr link) {
  MediaResult res = InitializeLink(link);

  if (res == MediaResult::OK) {
    fxl::MutexLocker locker(&mutex_);

    // Assert that we are the output in this link.
    FXL_DCHECK(this == link->GetOutput().get());

    if (is_shutting_down()) {
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

  if (is_shutting_down()) {
    return MediaResult::SHUTTING_DOWN;
  }

  auto iter = links_.find(link);
  if (iter == links_.end()) {
    return MediaResult::NOT_FOUND;
  }

  links_.erase(iter);
  return MediaResult::OK;
}

MediaResult AudioOutput::InitializeLink(
    const AudioRendererToOutputLinkPtr& link) {
  FXL_DCHECK(link);
  return MediaResult::OK;
}

void AudioOutput::Unlink() {
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

}  // namespace audio
}  // namespace media
