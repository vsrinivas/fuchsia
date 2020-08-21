// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ultrasound_renderer.h"

#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

UltrasoundRenderer::UltrasoundRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request, Context* context,
    fuchsia::ultrasound::Factory::CreateRendererCallback callback)
    : BaseRenderer(std::move(request), context), create_callback_(std::move(callback)) {
  FX_DCHECK(create_callback_);
  reporter().SetUsage(RenderUsage::ULTRASOUND);
}

fit::result<std::shared_ptr<ReadableStream>, zx_status_t> UltrasoundRenderer::InitializeDestLink(
    const AudioObject& dest) {
  if (!create_callback_) {
    return fit::error(ZX_ERR_BAD_STATE);
  }
  auto format = dest.format();
  if (!format) {
    return fit::error(ZX_ERR_BAD_STATE);
  }

  auto result = audio::clock::DuplicateClock(raw_clock());
  if (!result.is_ok()) {
    return fit::error(result.error());
  }
  zx::clock reference_clock_out = result.take_value();

  // Ultrasound renderers require FLOAT samples.
  auto stream_type = format->stream_type();
  stream_type.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  auto create_result = Format::Create(stream_type);
  FX_DCHECK(create_result.is_ok());

  format_ = {create_result.take_value()};
  create_callback_(std::move(reference_clock_out), format_->stream_type());
  create_callback_ = nullptr;
  return BaseRenderer::InitializeDestLink(dest);
}

void UltrasoundRenderer::CleanupDestLink(const AudioObject& dest) {
  // Ultrasound renderers do not support being re-linked. If we become unlinked then we will just
  // close the client channel.
  binding().Close(ZX_OK);
  BaseRenderer::CleanupDestLink(dest);
}

// Some unsupported methods on ultrasound renderers.
void UltrasoundRenderer::SetPcmStreamType(fuchsia::media::AudioStreamType format) {
  FX_LOGS(ERROR) << "Unsupported method SetPcmStreamType on ultrasound renderer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundRenderer::SetUsage(fuchsia::media::AudioRenderUsage usage) {
  FX_LOGS(ERROR) << "Unsupported method SetUsage on ultrasound renderer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundRenderer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  FX_LOGS(ERROR) << "Unsupported method BindGainControl on ultrasound renderer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundRenderer::SetReferenceClock(zx::clock ref_clock) {
  FX_LOGS(ERROR) << "Unsupported method SetReferenceClock on ultrasound renderer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace media::audio
