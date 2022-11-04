// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/ultrasound_renderer.h"

#include "src/media/audio/lib/clock/utils.h"

namespace media::audio {

namespace {
const PipelineConfig::MixGroup* FindUltrasoundGroup(const PipelineConfig::MixGroup& group) {
  auto& inputs = group.input_streams;
  if (std::find(inputs.begin(), inputs.end(), RenderUsage::ULTRASOUND) != inputs.end()) {
    return &group;
  }
  for (auto& child : group.inputs) {
    auto result = FindUltrasoundGroup(child);
    if (result) {
      return result;
    }
  }
  return nullptr;
}
}  // namespace

constexpr bool kLogUltrasoundRendererCtorDtor = false;

UltrasoundRenderer::UltrasoundRenderer(
    fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request, Context* context,
    fuchsia::ultrasound::Factory::CreateRendererCallback callback)
    : BaseRenderer(std::move(request), context), create_callback_(std::move(callback)) {
  FX_DCHECK(create_callback_);
  reporter().SetUsage(RenderUsage::ULTRASOUND);

  if constexpr (kLogUltrasoundRendererCtorDtor) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << this << ") *****";
  }
}

UltrasoundRenderer::~UltrasoundRenderer() {
  // We (not ~BaseRenderer) must call this, because our ReportStop is gone when parent dtor runs
  ReportStopIfStarted();

  if constexpr (kLogUltrasoundRendererCtorDtor) {
    FX_LOGS(INFO) << __FUNCTION__ << " (" << this << ") *****";
  }
}

fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t>
UltrasoundRenderer::InitializeDestLink(const AudioObject& dest) {
  if (!create_callback_) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }

  uint32_t channels;
  uint32_t frames_per_second;

  // UltrasoundRenderers use FLOAT samples, but the frame rate and channel count
  // are defined by the output pipeline we are connected to.
  auto pipeline_config = dest.pipeline_config();
  if (pipeline_config) {
    auto group = FindUltrasoundGroup(pipeline_config->root());
    if (!group) {
      FX_LOGS(ERROR) << "PipelineConfig missing ULTRASOUND group";
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    channels = group->output_channels;
    frames_per_second = group->output_rate;
  } else {
    auto format = dest.format();
    if (!format) {
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    channels = format->channels();
    frames_per_second = format->frames_per_second();
  }

  format_ = Format::Create({
                               .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                               .channels = channels,
                               .frames_per_second = frames_per_second,
                           })
                .take_value();

  auto reference_clock_result = reference_clock()->DuplicateZxClockReadOnly();
  if (!reference_clock_result) {
    return fpromise::error(ZX_ERR_INTERNAL);
  }

  reporter().SetFormat(*format_);
  create_callback_(std::move(*reference_clock_result), format_->stream_type());
  create_callback_ = nullptr;
  return BaseRenderer::InitializeDestLink(dest);
}

void UltrasoundRenderer::CleanupDestLink(const AudioObject& dest) {
  // Ultrasound renderers cannot be re-linked. If unlinked, we just close the client channel.
  binding().Close(ZX_OK);
  BaseRenderer::CleanupDestLink(dest);
}

void UltrasoundRenderer::ReportStart() {
  BaseRenderer::ReportStart();
  context().audio_admin().UpdateRendererState(RenderUsage::ULTRASOUND, true, this);
}

void UltrasoundRenderer::ReportStop() {
  BaseRenderer::ReportStop();
  context().audio_admin().UpdateRendererState(RenderUsage::ULTRASOUND, false, this);
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
