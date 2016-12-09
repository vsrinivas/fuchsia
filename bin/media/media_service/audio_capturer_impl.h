// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "apps/media/services/media_capturer.fidl.h"
#include "apps/media/src/audio/usb_audio_source.h"
#include "apps/media/src/fidl/fidl_packet_producer.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/framework/graph.h"

namespace media {

// Fidl agent that captures audio.
class AudioCapturerImpl : public MediaServiceImpl::Product<MediaCapturer>,
                          public MediaCapturer {
 public:
  static std::shared_ptr<AudioCapturerImpl> Create(
      fidl::InterfaceRequest<MediaCapturer> request,
      MediaServiceImpl* owner);

  ~AudioCapturerImpl() override;

  // MediaCapturer implementation.
  void GetSupportedMediaTypes(
      const GetSupportedMediaTypesCallback& callback) override;

  void SetMediaType(MediaTypePtr media_type) override;

  void GetPacketProducer(fidl::InterfaceRequest<MediaPacketProducer>
                             packet_producer_request) override;

  void Start() override;

  void Stop() override;

 private:
  AudioCapturerImpl(fidl::InterfaceRequest<MediaCapturer> request,
                    MediaServiceImpl* owner);

  Graph graph_;
  std::shared_ptr<UsbAudioSource> source_;
  std::shared_ptr<FidlPacketProducer> producer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AudioCapturerImpl);
};

}  // namespace media
