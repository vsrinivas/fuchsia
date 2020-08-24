// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream.h"

#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_call.h>

namespace audio {
namespace aml_g12 {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_ENABLE_GPIO,
  FRAGMENT_CODEC_0,
  FRAGMENT_CODEC_1,
  FRAGMENT_CODEC_2,
  FRAGMENT_CODEC_3,
  FRAGMENT_CODEC_4,
  FRAGMENT_CODEC_5,
  FRAGMENT_CODEC_6,
  FRAGMENT_CODEC_7,  // Support up to 8 codecs.
  FRAGMENT_COUNT,
};

constexpr size_t kMaxNumberOfChannels = 2;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
constexpr size_t kBytesPerSample = 2;
// Calculate ring buffer size for 1 second of 16-bit, max rate.
constexpr size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
    kMaxSampleRate * kBytesPerSample * kMaxNumberOfChannels, PAGE_SIZE);

AmlG12TdmStream::AmlG12TdmStream(zx_device_t* parent, bool is_input, ddk::PDev pdev,
                                 const ddk::GpioProtocolClient enable_gpio)
    : SimpleAudioStream(parent, is_input),
      pdev_(std::move(pdev)),
      enable_gpio_(std::move(enable_gpio)) {
  InitDaiFormats();  // For default configuration in HW initialization.
}

zx_status_t AmlG12TdmStream::InitHW() {
  zx_status_t status;

  // Shut down the SoC audio peripherals (tdm/dma)
  aml_audio_->Shutdown();

  auto on_error = fbl::MakeAutoCall([this]() { aml_audio_->Shutdown(); });

  aml_audio_->Initialize();

  // Setup TDM.
  uint8_t bits_per_bitoffset = 0;
  uint8_t number_of_slots = 0;
  uint8_t bits_per_slot = 0;
  uint8_t bits_per_sample = 0;
  constexpr uint32_t kMaxLanes = metadata::kMaxNumberOfLanes;
  uint32_t lanes_mutes[kMaxLanes] = {};
  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
    case metadata::TdmType::LeftJustified:
      // 4/3 bitoffset, 2 slots (regardless of number of channels), 32 bits/slot, 16 bits/sample.
      // Note: 3 bit offest places msb of sample one sclk period after edge of fsync
      // to provide i2s framing.
      if (metadata_.is_input) {
        if (metadata_.tdm.type == metadata::TdmType::I2s) {
          bits_per_bitoffset = 4;
        } else {
          bits_per_bitoffset = 6;
        }
      } else {
        if (metadata_.tdm.type == metadata::TdmType::I2s) {
          bits_per_bitoffset = 3;
        } else {
          bits_per_bitoffset = 5;
        }
      }
      number_of_slots = 2;
      bits_per_slot = 32;
      bits_per_sample = 16;

      // Configure lanes mute masks.
      for (size_t i = 0; i < kMaxLanes; ++i) {
        lanes_mutes[i] = (channels_to_use_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED)
                             ? ((~channels_to_use_) >> (i * 2)) & 3
                             : 0;
      }
      break;
    case metadata::TdmType::Pcm:
      if (metadata_.number_of_channels != 1) {
        zxlogf(ERROR, "%s Unsupported number of channels for PCM %d", __FILE__,
               metadata_.number_of_channels);
        return ZX_ERR_NOT_SUPPORTED;
      }
      // bitoffset = 4/3, 1 slot, 16 bits/slot, 32 bits/sample.
      // For output bitoffest 3 places msb of sample one sclk period after fsync to provide PCM
      // framing.
      bits_per_bitoffset = metadata_.is_input ? 4 : 3;
      number_of_slots = 1;
      bits_per_slot = 32;
      bits_per_sample = 16;

      lanes_mutes[0] =
          (channels_to_use_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED) ? (~channels_to_use_ & 1) : 0;
      break;
  }
  aml_audio_->ConfigTdmSlot(bits_per_bitoffset, number_of_slots - 1, bits_per_slot - 1,
                            bits_per_sample - 1, metadata_.mix_mask);
  aml_audio_->ConfigTdmSwaps(metadata_.swaps);
  for (size_t i = 0; i < kMaxLanes; ++i) {
    status = aml_audio_->ConfigTdmLane(i, metadata_.lanes_enable_mask[i], lanes_mutes[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure TDM lane %d", __FILE__, status);
      return status;
    }
  }

  if (metadata_.mClockDivFactor) {
    // PLL sourcing audio clock tree should be running at 768MHz
    // Note: Audio clock tree input should always be < 1GHz
    // mclk rate for 96kHz = 768MHz/5 = 153.6MHz
    // mclk rate for 48kHz = 768MHz/10 = 76.8MHz
    // Note: absmax mclk frequency is 500MHz per AmLogic
    ZX_ASSERT(!(metadata_.mClockDivFactor % 2));  // mClock div factor must be divisable by 2.
    uint32_t mdiv = metadata_.mClockDivFactor / ((dai_formats_[0].frame_rate == 96000) ? 2 : 1);
    status = aml_audio_->SetMclkDiv(mdiv - 1);  // register val is div - 1;
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
      return status;
    }
    aml_audio_->SetMClkPad(MCLK_PAD_0);
  }
  if (metadata_.sClockDivFactor) {
    // 48kHz: sclk=76.8MHz/25 = 3.072MHz, 3.072MHz/64=48kkHz
    // 96kHz: sclk=153.6MHz/25 = 6.144MHz, 6.144MHz/64=96kHz
    switch (metadata_.tdm.type) {
      case metadata::TdmType::I2s:
      case metadata::TdmType::LeftJustified:
        // lrduty = 32 sclk cycles (write 31) for i2s
        // invert sclk = true = sclk is rising edge in middle of bit for i2s
        status = aml_audio_->SetSclkDiv(metadata_.sClockDivFactor - 1, 31, 63,
                                        metadata_.tdm.type == metadata::TdmType::I2s);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
          return status;
        }
        break;
      case metadata::TdmType::Pcm:
        // lrduty = 1 sclk cycles (write 0) for PCM
        // TODO(andresoportus): For now we set lrduty to 2 sclk cycles (write 1), 1 does not work.
        // invert sclk = false = sclk is falling edge in middle of bit for PCM
        status = aml_audio_->SetSclkDiv(metadata_.sClockDivFactor, 1, 31, false);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
          return status;
        }
        break;
    }
  }

  // Allow clock divider changes to stabilize
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  aml_audio_->Sync();

  on_error.cancel();
  // At this point the SoC audio peripherals are ready to start, but no
  //  clocks are active.  The codec is also in software shutdown and will
  //  need to be started after the audio clocks are activated.
  return ZX_OK;
}

void AmlG12TdmStream::InitDaiFormats() {
  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
    dai_formats_[i].sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    dai_formats_[i].frame_rate = kMinSampleRate;
    dai_formats_[i].bits_per_sample = 16;
    dai_formats_[i].bits_per_channel = 32;
    dai_formats_[i].number_of_channels =
        metadata_.tdm.type == metadata::TdmType::I2s ? 2 : metadata_.codecs_number_of_channels[i];
    dai_formats_[i].channels_to_use.clear();
    for (uint32_t j = 0; j < 32; ++j) {
      if (metadata_.codecs_channels_mask[i] & (1 << j)) {
        dai_formats_[i].channels_to_use.push_back(j);
      }
    }
    switch (metadata_.tdm.type) {
      case metadata::TdmType::I2s:
        dai_formats_[i].justify_format = JUSTIFY_FORMAT_JUSTIFY_I2S;
        break;
      case metadata::TdmType::LeftJustified:
      case metadata::TdmType::Pcm:
        dai_formats_[i].justify_format = JUSTIFY_FORMAT_JUSTIFY_LEFT;
        break;
    }
  }

  channels_to_use_ = std::numeric_limits<uint64_t>::max();  // Enable all.
}

zx_status_t AmlG12TdmStream::InitPDev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return status;
  }

  size_t actual = 0;
  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                               sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  InitDaiFormats();

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  // Either only pdev or pdev + enable gpio + codecs.
  if (actual != 1 && actual != metadata_.tdm.number_of_codecs + 2) {
    zxlogf(ERROR, "%s could not get the correct number of fragments %lu", __FILE__, actual);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti - %d", __func__, status);
    return status;
  }

  ZX_ASSERT(metadata_.tdm.number_of_codecs <= 8);
  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
    codecs_.push_back(SimpleCodecClient());
    status = codecs_[i].SetProtocol(fragments[FRAGMENT_CODEC_0 + i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could set protocol - %d", __func__, status);
      return status;
    }
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    return status;
  }

  if (metadata_.is_input) {
    aml_tdm_in_t tdm = {};
    aml_toddr_t ddr = {};
    aml_tdm_mclk_t mclk = {};
    switch (metadata_.bus) {
      case metadata::AmlBus::TDM_A:
        tdm = TDM_IN_A;
        ddr = TODDR_A;
        mclk = MCLK_A;
        break;
      case metadata::AmlBus::TDM_B:
        tdm = TDM_IN_B;
        ddr = TODDR_B;
        mclk = MCLK_B;
        break;
      case metadata::AmlBus::TDM_C:
        tdm = TDM_IN_C;
        ddr = TODDR_C;
        mclk = MCLK_C;
        break;
    }
    aml_audio_ =
        AmlTdmInDevice::Create(*std::move(mmio), HIFI_PLL, tdm, ddr, mclk, metadata_.version);
  } else {
    aml_tdm_out_t tdm = {};
    aml_frddr_t ddr = {};
    aml_tdm_mclk_t mclk = {};
    switch (metadata_.bus) {
      case metadata::AmlBus::TDM_A:
        tdm = TDM_OUT_A;
        ddr = FRDDR_A;
        mclk = MCLK_A;
        break;
      case metadata::AmlBus::TDM_B:
        tdm = TDM_OUT_B;
        ddr = FRDDR_B;
        mclk = MCLK_B;
        break;
      case metadata::AmlBus::TDM_C:
        tdm = TDM_OUT_C;
        ddr = FRDDR_C;
        mclk = MCLK_C;
        break;
    }
    aml_audio_ =
        AmlTdmOutDevice::Create(*std::move(mmio), HIFI_PLL, tdm, ddr, mclk, metadata_.version);
  }
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "%s failed to create TDM device", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize the ring buffer
  status = InitBuffer(kRingBufferSize);
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

  status = InitHW();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init tdm hardware %d\n", __FILE__, status);
    return status;
  }

  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
    auto info = codecs_[i].GetInfo();
    if (info.is_error())
      return info.error_value();

    // Reset and initialize codec after we have configured I2S.
    status = codecs_[i].Reset();
    if (status != ZX_OK) {
      return status;
    }

    auto supported_formats = codecs_[i].GetDaiFormats();
    if (supported_formats.is_error()) {
      return supported_formats.error_value();
    }

    if (!codecs_[i].IsDaiFormatSupported(dai_formats_[i], supported_formats.value())) {
      zxlogf(ERROR, "%s codec does not support DAI format\n", __FILE__);
      return ZX_ERR_NOT_SUPPORTED;
    }

    status = codecs_[i].SetDaiFormat(dai_formats_[i]);
    if (status != ZX_OK) {
      return status;
    }

    codecs_[i].Start();
    if (status != ZX_OK) {
      return status;
    }
  }

  zxlogf(INFO, "audio: %s initialized", metadata_.is_input ? "input" : "output");
  return ZX_OK;
}

void AmlG12TdmStream::UpdateCodecsGainStateFromCurrent() {
  UpdateCodecsGainState({.gain_db = cur_gain_state_.cur_gain,
                         .muted = cur_gain_state_.cur_mute,
                         .agc_enable = cur_gain_state_.cur_agc});
}

void AmlG12TdmStream::UpdateCodecsGainState(GainState state) {
  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
    auto state2 = state;
    state2.gain_db += metadata_.tdm.codecs_delta_gains[i];
    if (override_mute_) {
      state2.muted = true;
    }
    codecs_[i].SetGainState(state2);
  }
}

zx_status_t AmlG12TdmStream::InitCodecsGain() {
  if (metadata_.tdm.number_of_codecs) {
    // Set our gain capabilities.
    float min_gain = std::numeric_limits<float>::lowest();
    float max_gain = std::numeric_limits<float>::max();
    float gain_step = std::numeric_limits<float>::lowest();
    bool can_all_mute = true;
    bool can_all_agc = true;
    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      auto format = codecs_[i].GetGainFormat();
      if (format.is_error()) {
        return format.error_value();
      }
      min_gain = std::max(min_gain, format->min_gain_db);
      max_gain = std::min(max_gain, format->max_gain_db);
      gain_step = std::max(gain_step, format->gain_step_db);
      can_all_mute = (can_all_mute && format->can_mute);
      can_all_agc = (can_all_agc && format->can_agc);
    }

    // Use first codec as reference initial gain.
    auto state = codecs_[0].GetGainState();
    if (state.is_error()) {
      return state.error_value();
    }
    cur_gain_state_.cur_gain = state->gain_db;
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
  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
      snprintf(device_name_, sizeof(device_name_), "%s-audio-i2s-%s", prod_name_, in_out);
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
      break;
    case metadata::TdmType::LeftJustified:
      snprintf(device_name_, sizeof(device_name_), "%s-audio-tdm-%s", prod_name_, in_out);
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
      break;
    case metadata::TdmType::Pcm:
      snprintf(device_name_, sizeof(device_name_), "%s-audio-pcm-%s", prod_name_, in_out);
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT;
      break;
  }

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
  for (size_t i = 0; i < metadata_.tdm.number_of_external_delays; ++i) {
    if (metadata_.tdm.external_delays[i].frequency == req.frames_per_second) {
      external_delay_nsec_ = metadata_.tdm.external_delays[i].nsecs;
      break;
    }
  }

  if (req.frames_per_second != dai_formats_[0].frame_rate ||
      req.channels_to_use_bitmask != channels_to_use_) {
    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      // Put codecs in safe state for rate change
      auto status = codecs_[i].Stop();
      // We allow codecs that do not support Stop.
      if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {
        zxlogf(ERROR, "%s failed to stop the codec", __FILE__);
        return status;
      }
    }

    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      dai_formats_[i].frame_rate = req.frames_per_second;
    }
    channels_to_use_ = req.channels_to_use_bitmask;
    auto status = InitHW();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s failed to reinitialize the HW", __FILE__);
      return status;
    }
    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      status = codecs_[i].SetDaiFormat(dai_formats_[i]);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to set the DAI format", __FILE__);
        return status;
      }

      // Restart codec
      status = codecs_[i].Start();
      // We allow codecs that do not support Start.
      if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {
        zxlogf(ERROR, "%s failed to restart the codec", __FILE__);
        return status;
      }
    }
  }
  return ZX_OK;
}

void AmlG12TdmStream::ShutdownHook() {
  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
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
  uint32_t rb_frames = static_cast<uint32_t>(pinned_ring_buffer_.region(0).size) / frame_size_;

  if (req.min_ring_buffer_frames > rb_frames) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  zx_status_t status;
  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  status = ring_buffer_vmo_.duplicate(rights, out_buffer);
  if (status != ZX_OK) {
    return status;
  }

  *out_num_rb_frames = rb_frames;

  aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, rb_frames * frame_size_);

  return ZX_OK;
}

zx_status_t AmlG12TdmStream::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ =
        static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                              (frame_size_ * dai_formats_[0].frame_rate / 1000 * notifs));
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

  range.min_channels = metadata_.number_of_channels;
  range.max_channels = metadata_.number_of_channels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
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
    zxlogf(ERROR, "%s buffer is not contiguous", __func__);
    return ZX_ERR_NO_MEMORY;
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

  composite_protocol_t composite;
  status = device_get_protocol(device, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  // Either only pdev or pdev + enable gpio + codecs.
  if (actual != 1 && actual != metadata.tdm.number_of_codecs + 2) {
    zxlogf(ERROR, "%s could not get the correct number of fragments %lu", __FILE__, actual);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (metadata.is_input) {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, true, fragments[FRAGMENT_PDEV],
        fragments[FRAGMENT_ENABLE_GPIO] ? fragments[FRAGMENT_ENABLE_GPIO]
                                        : ddk::GpioProtocolClient());
    if (stream == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);
  } else {
    auto stream = audio::SimpleAudioStream::Create<audio::aml_g12::AmlG12TdmStream>(
        device, false, fragments[FRAGMENT_PDEV],
        fragments[FRAGMENT_ENABLE_GPIO] ? fragments[FRAGMENT_ENABLE_GPIO]
                                        : ddk::GpioProtocolClient());
    if (stream == nullptr) {
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
ZIRCON_DRIVER_BEGIN(aml_tdm, audio::aml_g12::driver_ops, "aml-tdm", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
ZIRCON_DRIVER_END(aml_tdm)
    // clang-format on
