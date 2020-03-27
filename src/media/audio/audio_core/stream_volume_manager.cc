// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_volume_manager.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

std::string ToString(const fuchsia::media::AudioRenderUsage& render_usage) {
  switch (render_usage) {
    case fuchsia::media::AudioRenderUsage::MEDIA:
      return "Render::Media";
    case fuchsia::media::AudioRenderUsage::BACKGROUND:
      return "Render::Background";
    case fuchsia::media::AudioRenderUsage::SYSTEM_AGENT:
      return "Render::SystemAgent";
    case fuchsia::media::AudioRenderUsage::INTERRUPTION:
      return "Render::Interruption";
    case fuchsia::media::AudioRenderUsage::COMMUNICATION:
      return "Render::Communication";
    default:
      return "Unrecognized render usage";
  }
}

std::string ToString(const fuchsia::media::AudioCaptureUsage& capture_usage) {
  switch (capture_usage) {
    case fuchsia::media::AudioCaptureUsage::FOREGROUND:
      return "Capture::Foreground";
    case fuchsia::media::AudioCaptureUsage::BACKGROUND:
      return "Capture::Background";
    case fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT:
      return "Capture::SystemAgent";
    case fuchsia::media::AudioCaptureUsage::COMMUNICATION:
      return "Capture::Communication";
    default:
      return "Unrecognized render usage";
  }
}

std::string ToString(const fuchsia::media::Usage& usage) {
  if (usage.is_render_usage()) {
    return ToString(usage.render_usage());
  }

  return ToString(usage.capture_usage());
}

}  // namespace

StreamVolumeManager::VolumeSettingImpl::VolumeSettingImpl(fuchsia::media::Usage usage,
                                                          StreamVolumeManager* owner)
    : usage_(std::move(usage)), owner_(owner) {}

void StreamVolumeManager::VolumeSettingImpl::SetVolume(float volume) {
  owner_->SetUsageVolume(fidl::Clone(usage_), volume);
}

StreamVolumeManager::StreamVolumeManager(async_dispatcher_t* fidl_dispatcher)
    : render_usage_volume_setting_impls_{
          VolumeSettingImpl(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::BACKGROUND), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::INTERRUPTION), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::COMMUNICATION), this),
      },
      capture_usage_volume_setting_impls_{
          VolumeSettingImpl(fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::FOREGROUND), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT), this),
          VolumeSettingImpl(fuchsia::media::Usage::WithCaptureUsage(fuchsia::media::AudioCaptureUsage::COMMUNICATION), this),
      },
      render_usage_volume_controls_{
        VolumeControl(&render_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND)], fidl_dispatcher),
        VolumeControl(&render_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA)], fidl_dispatcher),
        VolumeControl(&render_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION)], fidl_dispatcher),
        VolumeControl(&render_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT)], fidl_dispatcher),
        VolumeControl(&render_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION)], fidl_dispatcher)
      },
      capture_usage_volume_controls_{
        VolumeControl(&capture_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::BACKGROUND)], fidl_dispatcher),
        VolumeControl(&capture_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::FOREGROUND)], fidl_dispatcher),
        VolumeControl(&capture_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)], fidl_dispatcher),
        VolumeControl(&capture_usage_volume_setting_impls_[fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::COMMUNICATION)], fidl_dispatcher),
      } {
  FX_DCHECK(fidl_dispatcher);

  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND) == 0);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA) == 1);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION) == 2);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT) == 3);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION) == 4);

  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::BACKGROUND) == 0);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::FOREGROUND) == 1);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT) == 2);
  static_assert(fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::COMMUNICATION) == 3);
}

const UsageGainSettings& StreamVolumeManager::GetUsageGainSettings() const {
  return usage_gain_settings_;
}

void StreamVolumeManager::SetUsageGain(fuchsia::media::Usage usage, float gain_db) {
  usage_gain_settings_.SetUsageGain(fidl::Clone(usage), gain_db);
  UpdateStreamsWithUsage(std::move(usage));
}

void StreamVolumeManager::SetUsageGainAdjustment(fuchsia::media::Usage usage, float gain_db) {
  usage_gain_settings_.SetUsageGainAdjustment(fidl::Clone(usage), gain_db);
  UpdateStreamsWithUsage(std::move(usage));
}

void StreamVolumeManager::BindUsageVolumeClient(
    fuchsia::media::Usage usage,
    fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request) {
  if (usage.is_render_usage()) {
    render_usage_volume_controls_[fidl::ToUnderlying(usage.render_usage())].AddBinding(
        std::move(request));
  } else {
    capture_usage_volume_controls_[fidl::ToUnderlying(usage.capture_usage())].AddBinding(
        std::move(request));
  }
}

void StreamVolumeManager::NotifyStreamChanged(StreamVolume* stream_volume) {
  UpdateStream(stream_volume, std::nullopt);
}

void StreamVolumeManager::NotifyStreamChanged(StreamVolume* stream_volume, Ramp ramp) {
  UpdateStream(stream_volume, ramp);
}

void StreamVolumeManager::AddStream(StreamVolume* stream_volume) {
  stream_volumes_.insert(stream_volume);
  UpdateStream(stream_volume, std::nullopt);
}

void StreamVolumeManager::RemoveStream(StreamVolume* stream_volume) {
  stream_volumes_.erase(stream_volume);
}

void StreamVolumeManager::SetUsageVolume(fuchsia::media::Usage usage, float volume) {
  AUD_VLOG(TRACE) << "Set usage " << ToString(usage) << " to volume " << volume;
  usage_volume_settings_.SetUsageVolume(fidl::Clone(usage), volume);
  UpdateStreamsWithUsage(std::move(usage));
}

void StreamVolumeManager::UpdateStreamsWithUsage(fuchsia::media::Usage usage) {
  for (auto& stream : stream_volumes_) {
    if (fidl::Equals(stream->GetStreamUsage(), usage)) {
      UpdateStream(stream, std::nullopt);
    }
  }
}

void StreamVolumeManager::UpdateStream(StreamVolume* stream, std::optional<Ramp> ramp) {
  const auto usage = stream->GetStreamUsage();
  const auto respects_policy_adjustments = stream->RespectsPolicyAdjustments();
  const auto usage_gain = respects_policy_adjustments
                              ? usage_gain_settings_.GetAdjustedUsageGain(fidl::Clone(usage))
                              : usage_gain_settings_.GetUnadjustedUsageGain(fidl::Clone(usage));
  const auto usage_volume = usage_volume_settings_.GetUsageVolume(std::move(usage));

  const auto gain_adjustment =
      stream->GetStreamMute() ? fuchsia::media::audio::MUTED_GAIN_DB : usage_gain;

  stream->RealizeVolume(VolumeCommand{usage_volume, gain_adjustment, ramp});
}

}  // namespace media::audio
