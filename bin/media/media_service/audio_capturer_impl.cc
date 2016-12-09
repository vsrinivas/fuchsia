// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/audio_capturer_impl.h"

#include "apps/media/lib/timeline.h"
#include "apps/media/src/audio/usb_audio_enum.h"
#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

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
      producer_(FidlPacketProducer::Create()) {
  UsbAudioEnum audio_enum;

  if (audio_enum.input_device_paths().empty()) {
    FTL_LOG(WARNING) << "No USB audio input devices found";
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        [this]() { UnbindAndReleaseFromOwner(); });
    return;
  }

  source_ = UsbAudioSource::Create(audio_enum.input_device_paths().front());

  graph_.ConnectParts(graph_.Add(source_), graph_.Add(producer_));
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
