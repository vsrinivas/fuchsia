// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fit/defer.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <numeric>
#include <optional>
#include <utility>

#include <pretty/hexdump.h>

#include "src/media/audio/drivers/aml-g12-tdm/aml_tdm-bind.h"

namespace audio {
namespace aml_g12 {

AmlG12TdmStream::AmlG12TdmStream(zx_device_t* parent, bool is_input, ddk::PDev pdev,
                                 const ddk::GpioProtocolClient enable_gpio)
    : SimpleAudioStream(parent, is_input),
      pdev_(std::move(pdev)),
      enable_gpio_(std::move(enable_gpio)) {
  status_time_ = inspect().GetRoot().CreateInt("status_time", 0);
  dma_status_ = inspect().GetRoot().CreateUint("dma_status", 0);
  tdm_status_ = inspect().GetRoot().CreateUint("tdm_status", 0);
  ring_buffer_physical_address_ = inspect().GetRoot().CreateUint("ring_buffer_physical_address", 0);
}

int AmlG12TdmStream::Thread() {
  while (1) {
    zx::time timestamp;
    irq_.wait(&timestamp);
    if (!running_.load()) {
      break;
    }
    zxlogf(ERROR, "DMA status: 0x%08X  TDM status: 0x%08X", aml_audio_->GetDmaStatus(),
           aml_audio_->GetTdmStatus());
    status_time_.Set(timestamp.get());
    dma_status_.Set(aml_audio_->GetDmaStatus());
    tdm_status_.Set(aml_audio_->GetTdmStatus());
  }
  zxlogf(INFO, "Exiting interrupt thread");
  return 0;
}

void AmlG12TdmStream::InitDaiFormats() {
  frame_rate_ =
      AmlTdmConfigDevice::kSupportedFrameRates[AmlTdmConfigDevice::kDefaultFrameRateIndex];
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
      case metadata::DaiType::Tdm2:
        dai_formats_[i].frame_format = FrameFormat::TDM2;
        break;
      case metadata::DaiType::Tdm3:
        dai_formats_[i].frame_format = FrameFormat::TDM3;
        break;
    }
  }
}

zx_status_t AmlG12TdmStream::InitPDev() {
  size_t actual = 0;
  zx_status_t status = device_get_fragment_metadata(
      parent(), "pdev", DEVICE_METADATA_PRIVATE, &metadata_, sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(
        ERROR,
        "device_get_metadata failed %d. Expected size %zu, got size %zu. Got metadata with value",
        status, sizeof(metadata::AmlConfig), actual);
    char output_buffer[80];
    for (size_t count = 0; count < actual; count += 16) {
      FILE* f = fmemopen(output_buffer, sizeof(output_buffer), "w");
      if (!f) {
        zxlogf(ERROR, "Couldn't open buffer. Returning.");
        return status;
      }
      hexdump_very_ex(reinterpret_cast<uint8_t*>(&metadata_) + count,
                      std::min(actual - count, 16UL), count, hexdump_stdio_printf, f);
      fclose(f);
      zxlogf(ERROR, "%s", output_buffer);
    }
    return status;
  }

  status = AmlTdmConfigDevice::Normalize(metadata_);
  if (status != ZX_OK) {
    return status;
  }
  InitDaiFormats();

  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "could not get pdev");
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not obtain bti - %d", status);
    return status;
  }

  ZX_ASSERT(metadata_.codecs.number_of_codecs <= 8);
  codecs_.reserve(metadata_.codecs.number_of_codecs);
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    codecs_.push_back(SimpleCodecClient());
    char fragment_name[32] = {};
    snprintf(fragment_name, 32, "codec-%02lu", i + 1);
    status = codecs_[i].SetProtocol(ddk::CodecProtocolClient(parent(), fragment_name));
    if (status != ZX_OK) {
      zxlogf(ERROR, "could set protocol - %s - %d", fragment_name, status);
      return status;
    }
  }

  std::optional<fdf::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get mmio %d", status);
    return status;
  }
  status = pdev_.GetInterrupt(0, 0, &irq_);
  if (status != ZX_ERR_OUT_OF_RANGE) {  // Not specified in the board file.
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not get IRQ %d", status);
      return status;
    }
    auto irq_thread = [](void* arg) -> int {
      return reinterpret_cast<AmlG12TdmStream*>(arg)->Thread();
    };
    running_.store(true);
    int rc = thrd_create_with_name(&thread_, irq_thread, this, "aml_tdm_irq_thread");
    if (rc != thrd_success) {
      zxlogf(ERROR, "could not create thread %d", rc);
      return status;
    }
  }

  aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, *std::move(mmio));
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "failed to create TDM device with config");
    return ZX_ERR_NO_MEMORY;
  }
  // Initial setup of one page of buffer, just to be safe.
  status = InitBuffer(zx_system_get_page_size());
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init buffer %d", status);
    return status;
  }
  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                                 pinned_ring_buffer_.region(0).size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to set buffer %d", status);
    return status;
  }

  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    auto info = codecs_[i].GetInfo();
    if (info.is_error()) {
      zxlogf(ERROR, "could get codec info %d", status);
      return info.error_value();
    }

    // Reset and initialize codec after we have configured I2S.
    status = codecs_[i].Reset();
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not reset codec %d", status);
      return status;
    }

    auto supported_formats = codecs_[i].GetDaiFormats();
    if (supported_formats.is_error()) {
      zxlogf(ERROR, "supported formats error %d", status);
      return supported_formats.error_value();
    }

    if (!IsDaiFormatSupported(dai_formats_[i], supported_formats.value())) {
      zxlogf(ERROR, "codec does not support DAI format");
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx::result<CodecFormatInfo> format_info = codecs_[i].SetDaiFormat(dai_formats_[i]);
    if (!format_info.is_ok()) {
      zxlogf(ERROR, "could not set DAI format %s", format_info.status_string());
      return format_info.status_value();
    }
  }

  // Put codecs in stopped state before starting the AMLogic engine.
  // Codecs are started after format is set via ChangeFormat() or the stream is explicitly started.
  status = StopAllCodecs();
  if (status != ZX_OK) {
    return status;
  }

  status = aml_audio_->InitHW(metadata_, std::numeric_limits<uint64_t>::max(), frame_rate_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init tdm hardware %d", status);
    return status;
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
        zxlogf(ERROR, "Could not get gain format %d", format.error_value());
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
      zxlogf(ERROR, "Could not get gain state %d", state.error_value());
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
    case metadata::DaiType::Tdm2:
      tdm_type = "tdm2";
      break;
    case metadata::DaiType::Tdm3:
      tdm_type = "tdm3";
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

zx_status_t AmlG12TdmStream::UpdateHardwareSettings() {
  zx_status_t status = StopAllCodecs();  // Put codecs in safe state for format changes.
  if (status != ZX_OK) {
    return status;
  }

  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    dai_formats_[i].frame_rate = frame_rate_;
  }
  status = aml_audio_->InitHW(metadata_, active_channels_, frame_rate_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to reinitialize the HW");
    return status;
  }

  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    zx::result<CodecFormatInfo> format_info = codecs_[i].SetDaiFormat(dai_formats_[i]);
    if (!format_info.is_ok()) {
      zxlogf(ERROR, "failed to set the DAI format");
      return format_info.status_value();
    }
    if (format_info->has_turn_on_delay()) {
      int64_t delay = format_info->turn_on_delay();
      codecs_turn_on_delay_nsec_ = std::max(codecs_turn_on_delay_nsec_, delay);
    }
    if (format_info->has_turn_off_delay()) {
      int64_t delay = format_info->turn_off_delay();
      codecs_turn_off_delay_nsec_ = std::max(codecs_turn_off_delay_nsec_, delay);
    }
  }
  status = StartAllEnabledCodecs();
  if (status != ZX_OK) {
    return status;
  }
  hardware_configured_ = true;
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::ChangeActiveChannels(uint64_t mask) {
  uint64_t old_mask = active_channels_;
  active_channels_ = mask;
  // Only stop the codecs for channels not active, not the AMLogic HW.
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    uint64_t codec_mask = metadata_.codecs.ring_buffer_channels_to_use_bitmask[i];
    bool enabled = mask & codec_mask;
    bool old_enabled = old_mask & codec_mask;
    if (enabled != old_enabled) {
      if (enabled) {
        zx_status_t status = StartCodecIfEnabled(i);
        if (status != ZX_OK) {
          return status;
        }
      } else {
        zx_status_t status = codecs_[i].Stop();
        if (status != ZX_OK) {
          zxlogf(ERROR, "Failed to stop the codec");
          return status;
        }
        constexpr uint32_t codecs_turn_off_delay_if_unknown_msec = 50;
        zx::duration delay = codecs_turn_off_delay_nsec_
                                 ? zx::nsec(codecs_turn_off_delay_nsec_)
                                 : zx::msec(codecs_turn_off_delay_if_unknown_msec);
        zx::nanosleep(zx::deadline_after(delay));
      }
    }
  }
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  auto cleanup = fit::defer([this, old_codecs_turn_on_delay_nsec = codecs_turn_on_delay_nsec_,
                             old_codecs_turn_off_delay_nsec = codecs_turn_off_delay_nsec_] {
    codecs_turn_on_delay_nsec_ = old_codecs_turn_on_delay_nsec;
    codecs_turn_off_delay_nsec_ = old_codecs_turn_off_delay_nsec;
  });
  fifo_depth_ = aml_audio_->fifo_depth();
  codecs_turn_on_delay_nsec_ = 0;
  codecs_turn_off_delay_nsec_ = 0;
  for (size_t i = 0; i < metadata_.codecs.number_of_external_delays; ++i) {
    if (metadata_.codecs.external_delays[i].frequency == req.frames_per_second) {
      external_delay_nsec_ = metadata_.codecs.external_delays[i].nsecs;
      break;
    }
  }
  if (!hardware_configured_ || req.frames_per_second != frame_rate_) {
    frame_rate_ = req.frames_per_second;
    zx_status_t status = UpdateHardwareSettings();
    if (status != ZX_OK) {
      return status;
    }
  }
  SetTurnOnDelay(codecs_turn_on_delay_nsec_);
  cleanup.cancel();
  return ZX_OK;
}

void AmlG12TdmStream::ShutdownHook() {
  if (running_.load()) {
    running_.store(false);
    irq_.destroy();
    thrd_join(thread_, NULL);
  }

  // safe the codec so it won't throw clock errors when tdm bus shuts down
  StopAllCodecs();

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

  size_t vmo_size = fbl::round_up<size_t, size_t>(ring_buffer_size, zx_system_get_page_size());
  auto status = InitBuffer(vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init buffer %d", status);
    return status;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  status = ring_buffer_vmo_.duplicate(rights, out_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to duplicate VMO %d", status);
    return status;
  }
  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to set buffer %d", status);
    return status;
  }
  ring_buffer_physical_address_.Set(pinned_ring_buffer_.region(0).phys_addr);

  // This is safe because of the overflow check we made above.
  *out_num_rb_frames = static_cast<uint32_t>(out_frames);
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();
  zx_status_t status = StartAllEnabledCodecs();
  if (status != ZX_OK) {
    aml_audio_->Stop();
    return status;
  }

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

zx_status_t AmlG12TdmStream::StartCodecIfEnabled(size_t index) {
  if (!metadata_.codecs.ring_buffer_channels_to_use_bitmask[index]) {
    zxlogf(ERROR, "Codec %zu must configure ring_buffer_channels_to_use_bitmask", index);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Enable codec depending on the mapping provided by metadata_
  // metadata_.ring_buffer.number_of_channels are mapped into metadata_.codecs.number_of_codecs
  // The active_channels_ bitmask defines which ring buffer channels corresponding codecs to start
  // metadata_.codecs.ring_buffer_channels_to_use_bitmask[] specifies which channel in the ring
  // buffer a codec is associated with. If the codecs ring_buffer_channels_to_use_bitmask intersects
  // with active_channels_ then start the codec.
  if (active_channels_ & metadata_.codecs.ring_buffer_channels_to_use_bitmask[index]) {
    zx_status_t status = codecs_[index].Start();
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to start the codec");
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::StartAllEnabledCodecs() {
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    zx_status_t status = StartCodecIfEnabled(i);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Stop() {
  override_mute_ = true;
  UpdateCodecsGainStateFromCurrent();
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  zx_status_t status = StopAllCodecs();
  if (status != ZX_OK) {
    return status;
  }
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::StopAllCodecs() {
  for (size_t i = 0; i < metadata_.codecs.number_of_codecs; ++i) {
    auto status = codecs_[i].Stop();
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to stop the codec");
      return status;
    }
  }
  constexpr uint32_t codecs_turn_off_delay_if_unknown_msec = 50;
  zx::duration delay = codecs_turn_off_delay_nsec_
                           ? zx::nsec(codecs_turn_off_delay_nsec_)
                           : zx::msec(codecs_turn_off_delay_if_unknown_msec);
  zx::nanosleep(zx::deadline_after(delay));
  return ZX_OK;
}

zx_status_t AmlG12TdmStream::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  SimpleAudioStream::SupportedFormat format = {};

  format.range.min_channels = metadata_.ring_buffer.number_of_channels;
  format.range.max_channels = metadata_.ring_buffer.number_of_channels;
  ZX_ASSERT(metadata_.ring_buffer.bytes_per_sample == 2);
  format.range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;

  for (size_t i = 0; i < metadata_.ring_buffer.number_of_channels; ++i) {
    if (metadata_.ring_buffer.frequency_ranges[i].min_frequency ||
        metadata_.ring_buffer.frequency_ranges[i].max_frequency) {
      SimpleAudioStream::FrequencyRange range = {};
      range.min_frequency = metadata_.ring_buffer.frequency_ranges[i].min_frequency;
      range.max_frequency = metadata_.ring_buffer.frequency_ranges[i].max_frequency;
      format.frequency_ranges.push_back(std::move(range));
    }
  }

  for (auto& i : AmlTdmConfigDevice::kSupportedFrameRates) {
    format.range.min_frames_per_second = i;
    format.range.max_frames_per_second = i;
    format.range.flags =
        ASF_RANGE_FLAG_FPS_CONTINUOUS;  // No need to specify family when min == max.
    supported_formats_.push_back(format);
  }

  return ZX_OK;
}

zx_status_t AmlG12TdmStream::InitBuffer(size_t size) {
  // Make sure the DMA is stopped before releasing quarantine.
  aml_audio_->Stop();
  // Make sure that all reads/writes have gone through.
#if defined(__aarch64__)
  __asm__ volatile("dsb sy" : : : "memory");
#endif
  auto status = bti_.release_quarantine();
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not release quarantine bti - %d", status);
    return status;
  }
  pinned_ring_buffer_.Unpin();
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to allocate ring buffer vmo - %d", status);
    return status;
  }

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to pin ring buffer vmo - %d", status);
    return status;
  }
  if (pinned_ring_buffer_.region_count() != 1) {
    if (!AllowNonContiguousRingBuffer()) {
      zxlogf(ERROR, "buffer is not contiguous");
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

static zx_status_t audio_bind(void* ctx, zx_device_t* device) {
  size_t actual = 0;
  metadata::AmlConfig metadata = {};
  auto status = device_get_fragment_metadata(device, "pdev", DEVICE_METADATA_PRIVATE, &metadata,
                                             sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "device_get_metadata failed %d", status);
    return status;
  }
  if (metadata.is_input) {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, true, ddk::PDev::FromFragment(device),
        ddk::GpioProtocolClient(device, "gpio-enable"));
    if (stream == nullptr) {
      zxlogf(ERROR, "Could not create aml-g12-tdm driver");
      return ZX_ERR_NO_MEMORY;
    }
    [[maybe_unused]] auto unused = fbl::ExportToRawPtr(&stream);
  } else {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, false, ddk::PDev::FromFragment(device),
        ddk::GpioProtocolClient(device, "gpio-enable"));
    if (stream == nullptr) {
      zxlogf(ERROR, "Could not create aml-g12-tdm driver");
      return ZX_ERR_NO_MEMORY;
    }
    [[maybe_unused]] auto unused = fbl::ExportToRawPtr(&stream);
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
