// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream.h"

#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <numeric>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <fbl/auto_call.h>

#include "src/media/audio/drivers/aml-g12-tdm/aml_tdm-bind.h"

namespace audio {
namespace aml_g12 {

AmlG12TdmStream::AmlG12TdmStream(zx_device_t* parent, bool is_input, ddk::PDev pdev,
                                 const ddk::GpioProtocolClient enable_gpio)
    : SimpleAudioStream(parent, is_input),
      pdev_(std::move(pdev)),
      enable_gpio_(std::move(enable_gpio)) {}

void AmlG12TdmStream::InitDaiFormats() {
  frame_rate_ = AmlTdmConfigDevice::kSupportedFrameRates[0];
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    // Only the PCM signed sample format is supported.
    dai_formats_[i].sample_format = SampleFormat::PCM_SIGNED;
    dai_formats_[i].frame_rate = frame_rate_;
    dai_formats_[i].bits_per_sample = metadata_.dai.bits_per_sample;
    dai_formats_[i].bits_per_slot = metadata_.dai.bits_per_slot;
    dai_formats_[i].number_of_channels = metadata_.dai.number_of_channels;
    dai_formats_[i].channels_to_use_bitmask = metadata_.codecs.channels_to_use_bitmask[i];
    switch (metadata_.dai.type) {
      case metadata::DaiType::I2s:
        dai_formats_[i].frame_format = FrameFormat::I2S;
        break;
      case metadata::DaiType::StereoLeftJustified:
        dai_formats_[i].frame_format = FrameFormat::STEREO_LEFT;
        break;
      case metadata::DaiType::Tdm1:
        dai_formats_[i].frame_format = FrameFormat::TDM1;
        break;
      default:
        ZX_ASSERT(0);  // Not supported.
    }
  }

  channels_to_use_ = std::numeric_limits<uint64_t>::max();  // Enable all.
}

zx_status_t AmlG12TdmStream::InitPDev() {
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  size_t actual = 0;
  zx_status_t status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                           sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  status = AmlTdmConfigDevice::Normalize(metadata_);
  if (status != ZX_OK) {
    return status;
  }
  InitDaiFormats();

  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "%s could not get pdev", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti - %d", __func__, status);
    return status;
  }

  ZX_ASSERT(metadata_.codecs.number_of_codecs <= 8);
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    codecs_.push_back(SimpleCodecClient());
    char fragment_name[32] = {};
    snprintf(fragment_name, 32, "codec-%02lu", i + 1);
    status = codecs_[i].SetProtocol(ddk::CodecProtocolClient(composite, fragment_name));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could set protocol - %s - %d", __func__, fragment_name, status);
      return status;
    }
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get mmio %d", __func__, status);
    return status;
  }

  aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, *std::move(mmio));
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "%s failed to create TDM device with config", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  // Initial setup of one page of buffer, just to be safe.
  status = InitBuffer(PAGE_SIZE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init buffer %d", __FILE__, status);
    return status;
  }
  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                                 pinned_ring_buffer_.region(0).size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set buffer %d", __FILE__, status);
    return status;
  }

  status = aml_audio_->InitHW(metadata_, channels_to_use_, frame_rate_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init tdm hardware %d\n", __FILE__, status);
    return status;
  }

  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    auto info = codecs_[i].GetInfo();
    if (info.is_error()) {
      zxlogf(ERROR, "%s could get codec info %d", __func__, status);
      return info.error_value();
    }

    // Reset and initialize codec after we have configured I2S.
    status = codecs_[i].Reset();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not reset codec %d", __func__, status);
      return status;
    }

    auto supported_formats = codecs_[i].GetDaiFormats();
    if (supported_formats.is_error()) {
      zxlogf(ERROR, "%s supported formats error %d", __func__, status);
      return supported_formats.error_value();
    }

    if (!IsDaiFormatSupported(dai_formats_[i], supported_formats.value())) {
      zxlogf(ERROR, "%s codec does not support DAI format\n", __FILE__);
      return ZX_ERR_NOT_SUPPORTED;
    }

    status = codecs_[i].SetDaiFormat(dai_formats_[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not set DAI format %d", __func__, status);
      return status;
    }

    codecs_[i].Start();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not start codec %d", __func__, status);
      return status;
    }
  }

  zxlogf(INFO, "audio: %s initialized", metadata_.is_input ? "input" : "output");
  return ZX_OK;
}

void AmlG12TdmStream::UpdateCodecsGainStateFromCurrent() {
  UpdateCodecsGainState({.gain = cur_gain_state_.cur_gain,
                         .muted = cur_gain_state_.cur_mute,
                         .agc_enabled = cur_gain_state_.cur_agc});
}

void AmlG12TdmStream::UpdateCodecsGainState(GainState state) {
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    auto state2 = state;
    state2.gain += metadata_.codecs.delta_gains[i];
    if (override_mute_) {
      state2.muted = true;
    }
    codecs_[i].SetGainState(state2);
  }
}

zx_status_t AmlG12TdmStream::InitCodecsGain() {
  if (metadata_.codecs.number_of_codecs) {
    // Set our gain capabilities.
    float min_gain = std::numeric_limits<float>::lowest();
    float max_gain = std::numeric_limits<float>::max();
    float gain_step = std::numeric_limits<float>::lowest();
    bool can_all_mute = true;
    bool can_all_agc = true;
    for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
      auto format = codecs_[i].GetGainFormat();
      if (format.is_error()) {
        zxlogf(ERROR, "%s Could not get gain format %d", __FILE__, format.error_value());
        return format.error_value();
      }
      min_gain = std::max(min_gain, format->min_gain);
      max_gain = std::min(max_gain, format->max_gain);
      gain_step = std::max(gain_step, format->gain_step);
      can_all_mute = (can_all_mute && format->can_mute);
      can_all_agc = (can_all_agc && format->can_agc);
    }

    // Use first codec as reference initial gain.
    auto state = codecs_[0].GetGainState();
    if (state.is_error()) {
      zxlogf(ERROR, "%s Could not get gain state %d", __FILE__, state.error_value());
      return state.error_value();
    }
    cur_gain_state_.cur_gain = state->gain;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;
    UpdateCodecsGainState(state.value());

    cur_gain_state_.min_gain = min_gain;
    cur_gain_state_.max_gain = max_gain;
    cur_gain_state_.gain_step = gain_step;
    cur_gain_state_.can_mute = can_all_mute;
    cur_gain_state_.can_agc = can_all_agc;
  } else {
    cur_gain_state_.cur_gain = 0.f;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;

    cur_gain_state_.min_gain = 0.f;
    cur_gain_state_.max_gain = 0.f;
    cur_gain_state_.gain_step = .0f;
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;
  }
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Init() {
  zx_status_t status;

  status = InitPDev();
  if (status != ZX_OK) {
    return status;
  }

  status = AddFormats();
  if (status != ZX_OK) {
    return status;
  }

  status = InitCodecsGain();
  if (status != ZX_OK) {
    return status;
  }

  const char* in_out = "out";
  if (metadata_.is_input) {
    in_out = "in";
  }
  strncpy(mfr_name_, metadata_.manufacturer, sizeof(mfr_name_));
  strncpy(prod_name_, metadata_.product_name, sizeof(prod_name_));
  unique_id_ = metadata_.unique_id;
  const char* tdm_type = nullptr;
  switch (metadata_.dai.type) {
    case metadata::DaiType::I2s:
      tdm_type = "i2s";
      break;
    case metadata::DaiType::StereoLeftJustified:
      tdm_type = "ljt";
      break;
    case metadata::DaiType::Tdm1:
      tdm_type = "tdm1";
      break;
  }
  snprintf(device_name_, sizeof(device_name_), "%s-audio-%s-%s", prod_name_, tdm_type, in_out);

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

// Timer handler for sending out position notifications
void AmlG12TdmStream::ProcessRingNotification() {
  ScopedToken t(domain_token());
  if (us_per_notification_) {
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    notify_timer_.Cancel();
    return;
  }

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = aml_audio_->GetRingPosition();
  NotifyPosition(resp);
}

zx_status_t AmlG12TdmStream::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = aml_audio_->fifo_depth();
  for (size_t i = 0; i < metadata_.codecs.number_of_external_delays; ++i) {
    if (metadata_.codecs.external_delays[i].frequency == req.frames_per_second) {
      external_delay_nsec_ = metadata_.codecs.external_delays[i].nsecs;
      break;
    }
  }
  if (req.frames_per_second != frame_rate_ || req.channels_to_use_bitmask != channels_to_use_) {
    for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
      // Put codecs in safe state for rate change
      auto status = codecs_[i].Stop();
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to stop the codec", __FILE__);
        return status;
      }
    }

    frame_rate_ = req.frames_per_second;
    for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
      dai_formats_[i].frame_rate = frame_rate_;
    }
    channels_to_use_ = req.channels_to_use_bitmask;
    auto status = aml_audio_->InitHW(metadata_, channels_to_use_, frame_rate_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s failed to reinitialize the HW", __FILE__);
      return status;
    }
    for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
      status = codecs_[i].SetDaiFormat(dai_formats_[i]);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to set the DAI format", __FILE__);
        return status;
      }

      // Restart codec
      status = codecs_[i].Start();
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to restart the codec", __FILE__);
        return status;
      }
    }
  }
  return ZX_OK;
}

void AmlG12TdmStream::ShutdownHook() {
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    // safe the codec so it won't throw clock errors when tdm bus shuts down
    codecs_[i].Stop();
  }
  if (enable_gpio_.is_valid()) {
    enable_gpio_.Write(0);
  }
  aml_audio_->Shutdown();
  pinned_ring_buffer_.Unpin();
}

zx_status_t AmlG12TdmStream::SetGain(const audio_proto::SetGainReq& req) {
  // Modify parts of the gain state we have received in the request.
  if (req.flags & AUDIO_SGF_MUTE_VALID) {
    cur_gain_state_.cur_mute = req.flags & AUDIO_SGF_MUTE;
  }
  if (req.flags & AUDIO_SGF_AGC_VALID) {
    cur_gain_state_.cur_agc = req.flags & AUDIO_SGF_AGC;
  };
  cur_gain_state_.cur_gain = req.gain;
  UpdateCodecsGainStateFromCurrent();
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                       uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  size_t ring_buffer_size =
      fbl::round_up<size_t, size_t>(req.min_ring_buffer_frames * frame_size_,
                                    std::lcm(frame_size_, aml_audio_->GetBufferAlignment()));
  size_t out_frames = ring_buffer_size / frame_size_;
  if (out_frames > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t vmo_size = fbl::round_up<size_t, size_t>(ring_buffer_size, PAGE_SIZE);
  auto status = InitBuffer(vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init buffer %d", __FILE__, status);
    return status;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  status = ring_buffer_vmo_.duplicate(rights, out_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to duplicate VMO %d", __FILE__, status);
    return status;
  }
  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set buffer %d", __FILE__, status);
    return status;
  }
  // This is safe because of the overflow check we made above.
  *out_num_rb_frames = static_cast<uint32_t>(out_frames);
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ = static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                                                 (frame_size_ * frame_rate_ / 1000 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  override_mute_ = false;
  UpdateCodecsGainStateFromCurrent();
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Stop() {
  override_mute_ = true;
  UpdateCodecsGainStateFromCurrent();
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  // Add the range for basic audio support.
  audio_stream_format_range_t range;

  range.min_channels = metadata_.ring_buffer.number_of_channels;
  range.max_channels = metadata_.ring_buffer.number_of_channels;
  ZX_ASSERT(metadata_.ring_buffer.bytes_per_sample == 2);
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  ZX_ASSERT(sizeof(AmlTdmConfigDevice::kSupportedFrameRates) / sizeof(uint32_t) == 2);
  ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[0] == 48'000);
  ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[1] == 96'000);
  range.min_frames_per_second = AmlTdmConfigDevice::kSupportedFrameRates[0];
  range.max_frames_per_second = AmlTdmConfigDevice::kSupportedFrameRates[1];
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t AmlG12TdmStream::InitBuffer(size_t size) {
  // Make sure the DMA is stopped before releasing quarantine.
  aml_audio_->Stop();
  // Make sure that all reads/writes have gone through.
#if defined(__aarch64__)
  asm __volatile__("dsb sy");
#endif
  auto status = bti_.release_quarantine();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not release quarantine bti - %d", __func__, status);
    return status;
  }
  pinned_ring_buffer_.Unpin();
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d", __func__, status);
    return status;
  }

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d", __func__, status);
    return status;
  }
  if (pinned_ring_buffer_.region_count() != 1) {
    if (!AllowNonContiguousRingBuffer()) {
      zxlogf(ERROR, "%s buffer is not contiguous", __func__);
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

static zx_status_t audio_bind(void* ctx, zx_device_t* device) {
  size_t actual = 0;
  metadata::AmlConfig metadata = {};
  auto status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &metadata,
                                    sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  ddk::CompositeProtocolClient composite(device);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  if (metadata.is_input) {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, true, ddk::PDev(composite), ddk::GpioProtocolClient(composite, "gpio-enable"));
    if (stream == nullptr) {
      zxlogf(ERROR, "%s Could not create aml-g12-tdm driver", __FILE__);
      return ZX_ERR_NO_MEMORY;
    }
    __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);
  } else {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, false, ddk::PDev(composite), ddk::GpioProtocolClient(composite, "gpio-enable"));
    if (stream == nullptr) {
      zxlogf(ERROR, "%s Could not create aml-g12-tdm driver", __FILE__);
      return ZX_ERR_NO_MEMORY;
    }
    __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = audio_bind;
  return ops;
}();

}  // namespace aml_g12
}  // namespace audio

// clang-format off
ZIRCON_DRIVER(aml_tdm, audio::aml_g12::driver_ops, "aml-tdm", "0.1");
