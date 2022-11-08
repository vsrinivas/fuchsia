// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_CAPTURER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_CAPTURER_H_

#include <fuchsia/ultrasound/cpp/fidl.h>

#include "src/media/audio/audio_core/shared/stream_usage.h"
#include "src/media/audio/audio_core/v1/base_capturer.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"

namespace media::audio {

class UltrasoundCapturer : public BaseCapturer {
 public:
  static std::shared_ptr<UltrasoundCapturer> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request, Context* context,
      fuchsia::ultrasound::Factory::CreateCapturerCallback callback) {
    return std::make_shared<UltrasoundCapturer>(std::move(request), context, std::move(callback));
  }

  // Callers should use the |Create| method instead, this is only public to enable std::make_shared.
  UltrasoundCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request,
                     Context* context,
                     fuchsia::ultrasound::Factory::CreateCapturerCallback callback);
  ~UltrasoundCapturer() override = default;

 private:
  // |media::audio::AudioObject|
  std::optional<Format> format() const final { return format_; }
  std::optional<StreamUsage> usage() const override {
    return {StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)};
  }
  fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) override;
  void CleanupSourceLink(const AudioObject& source,
                         std::shared_ptr<ReadableStream> stream) override;
  void SetRoutingProfile(bool routable) override {
    context().route_graph().SetCapturerRoutingProfile(
        *this, RoutingProfile{.routable = routable,
                              .usage = StreamUsage::WithCaptureUsage(CaptureUsage::ULTRASOUND)});
  }

  // |fuchsia::media::BaseCapturer|
  void ReportStart() final;
  void ReportStop() final;
  // Unsupported by UltrasoundCapturer
  void SetUsage(fuchsia::media::AudioCaptureUsage usage) final;
  void SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) final;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;
  void SetReferenceClock(zx::clock ref_clock) final;

  std::optional<Format> format_;
  fuchsia::ultrasound::Factory::CreateCapturerCallback create_callback_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_CAPTURER_H_
