// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_RENDERER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_RENDERER_H_

#include <fuchsia/ultrasound/cpp/fidl.h>

#include "src/media/audio/audio_core/v1/base_renderer.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/audio_core/v1/stream_volume_manager.h"

namespace media::audio {

class UltrasoundRenderer : public BaseRenderer {
 public:
  static std::shared_ptr<UltrasoundRenderer> Create(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request, Context* context,
      fuchsia::ultrasound::Factory::CreateRendererCallback callback) {
    return std::make_shared<UltrasoundRenderer>(std::move(request), context, std::move(callback));
  }

  // Callers should use the |Create| method instead, this is only public to enable std::make_shared.
  UltrasoundRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
                     Context* context,
                     fuchsia::ultrasound::Factory::CreateRendererCallback callback);
  ~UltrasoundRenderer() override;

 private:
  // |media::audio::AudioObject|
  std::optional<Format> format() const final { return format_; }
  std::optional<StreamUsage> usage() const override {
    return {StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND)};
  }
  fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;
  void CleanupDestLink(const AudioObject& dest) override;

  // |fuchsia::media::BaseRenderer|
  void ReportStart() final;
  void ReportStop() final;

  // |fuchsia::media::BaseRenderer| but unsupported by UltrasoundRenderer
  void SetPcmStreamType(fuchsia::media::AudioStreamType format) final;
  void SetUsage(fuchsia::media::AudioRenderUsage usage) final;
  void BindGainControl(fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) final;
  void SetReferenceClock(zx::clock ref_clock) final;

  std::optional<Format> format_;
  fuchsia::ultrasound::Factory::CreateRendererCallback create_callback_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_ULTRASOUND_RENDERER_H_
