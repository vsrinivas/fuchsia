// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/audio_capturer_impl.h"

#include "garnet/bin/media/audio/audio_input.h"
#include "garnet/bin/media/audio/audio_input_enum.h"
#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media {

// static
std::shared_ptr<AudioCapturerImpl> AudioCapturerImpl::Create(
    fidl::InterfaceRequest<MediaCapturer> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<AudioCapturerImpl>(
      new AudioCapturerImpl(std::move(request), owner));
}

AudioCapturerImpl::AudioCapturerImpl(
    fidl::InterfaceRequest<MediaCapturer> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaCapturer>(this, std::move(request), owner),
      graph_(owner->multiproc_task_runner()),
      producer_(FidlPacketProducer::Create()) {
  AudioInputEnum audio_enum;

  if (audio_enum.input_devices().empty()) {
    FXL_LOG(WARNING) << "No audio input devices found";
    RCHECK(false);
    return;
  }

  // Select the most recently plugged in audio input
  size_t best = 0;
  for (size_t i = 0; i < audio_enum.input_devices().size(); ++i) {
    if (audio_enum.input_devices()[i].plug_time >
        audio_enum.input_devices()[best].plug_time) {
      best = i;
    }
  }

  source_ = AudioInput::Create(audio_enum.input_devices()[best].path);
  graph_.ConnectNodes(graph_.Add(source_), graph_.Add(producer_));
  graph_.Prepare();
}

AudioCapturerImpl::~AudioCapturerImpl() {}

void AudioCapturerImpl::GetSupportedMediaTypes(
    const GetSupportedMediaTypesCallback& callback) {
  if (!source_) {
    callback(fidl::Array<MediaTypeSetPtr>());
    return;
  }

  callback(
      fidl::Array<MediaTypeSetPtr>::From(source_->GetSupportedStreamTypes()));
}

void AudioCapturerImpl::SetMediaType(MediaTypePtr media_type) {
  if (!source_) {
    return;
  }

  source_->SetStreamType(media_type.To<std::unique_ptr<StreamType>>());
}

void AudioCapturerImpl::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> packet_producer_request) {
  if (!producer_) {
    return;
  }

  producer_->Bind(std::move(packet_producer_request));
}

void AudioCapturerImpl::Start() {
  if (!source_) {
    return;
  }

  source_->Start();
}

void AudioCapturerImpl::Stop() {
  if (!source_) {
    return;
  }

  source_->Stop();
}

}  // namespace media
