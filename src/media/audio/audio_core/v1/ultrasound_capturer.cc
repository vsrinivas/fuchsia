// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/ultrasound_capturer.h"

#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

UltrasoundCapturer::UltrasoundCapturer(
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request, Context* context,
    fuchsia::ultrasound::Factory::CreateCapturerCallback callback)
    : BaseCapturer(std::nullopt, std::move(request), context),
      create_callback_(std::move(callback)) {
  FX_DCHECK(create_callback_);
  reporter().SetUsage(CaptureUsage::ULTRASOUND);
}

fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
UltrasoundCapturer::InitializeSourceLink(const AudioObject& source,
                                         std::shared_ptr<ReadableStream> stream) {
  if (!create_callback_) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }
  auto format = source.format();
  if (!format) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  auto reference_clock_result = reference_clock()->DuplicateZxClockReadOnly();
  if (!reference_clock_result) {
    return fpromise::error(ZX_ERR_INTERNAL);
  }

  // Ultrasound renderers require FLOAT samples.
  auto stream_type = format->stream_type();
  stream_type.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  auto create_result = Format::Create(stream_type);
  FX_DCHECK(create_result.is_ok());

  UpdateFormat(create_result.value());
  format_ = {create_result.take_value()};
  create_callback_(std::move(*reference_clock_result), format_->stream_type());
  create_callback_ = nullptr;
  return BaseCapturer::InitializeSourceLink(source, std::move(stream));
}

void UltrasoundCapturer::CleanupSourceLink(const AudioObject& source,
                                           std::shared_ptr<ReadableStream> stream) {
  // Ultrasound capturers cannot be re-linked. If unlinked, we just close the client channel.
  binding().Close(ZX_OK);
  BaseCapturer::CleanupSourceLink(source, std::move(stream));
}

void UltrasoundCapturer::ReportStart() {
  BaseCapturer::ReportStart();
  context().audio_admin().UpdateCapturerState(CaptureUsage::ULTRASOUND, true, this);
}

void UltrasoundCapturer::ReportStop() {
  BaseCapturer::ReportStop();
  context().audio_admin().UpdateCapturerState(CaptureUsage::ULTRASOUND, false, this);
}

void UltrasoundCapturer::SetUsage(fuchsia::media::AudioCaptureUsage usage) {
  FX_LOGS(ERROR) << "Unsupported method SetUsage on ultrasound capturer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundCapturer::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  FX_LOGS(ERROR) << "Unsupported method SetPcmStreamType on ultrasound capturer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundCapturer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  FX_LOGS(ERROR) << "Unsupported method BindGainControl on ultrasound capturer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}
void UltrasoundCapturer::SetReferenceClock(zx::clock ref_clock) {
  FX_LOGS(ERROR) << "Unsupported method SetReferenceClock on ultrasound capturer";
  binding().Close(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace media::audio
