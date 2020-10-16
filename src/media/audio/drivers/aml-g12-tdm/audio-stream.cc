// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream.h"

#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_call.h>

#include "src/media/audio/drivers/aml-g12-tdm/aml_tdm-bind.h"

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
  constexpr uint32_t kMaxLanes = metadata::kMaxNumberOfLanes;
  uint32_t lanes_mutes[kMaxLanes] = {};
  // bitoffset defines samples start relative to the edge of fsync.
  uint8_t bitoffset = metadata_.is_input ? 4 : 3;
  if (metadata_.tdm.type == metadata::TdmType::I2s) {
    bitoffset--;
  }
  if (metadata_.tdm.sclk_on_raising) {
    bitoffset--;
  }

  // Configure lanes mute masks based on channels_to_use_ and lane enable mask.
  if (channels_to_use_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED) {
    uint32_t channel = 0;
    size_t lane_start = 0;
    for (size_t i = 0; i < kMaxLanes; ++i) {
      for (size_t j = 0; j < 64; ++j) {
        if (metadata_.lanes_enable_mask[i] & (static_cast<uint64_t>(1) << j)) {
          if (~channels_to_use_ & (1 << channel)) {
            lanes_mutes[i] |= ((~channels_to_use_ & (1 << channel)) >> lane_start);
          }
          channel++;
        }
      }
      lane_start = channel;
    }
  }
  aml_audio_->ConfigTdmSlot(bitoffset, static_cast<uint8_t>(metadata_.dai_number_of_channels - 1),
                            metadata_.tdm.bits_per_slot - 1, metadata_.tdm.bits_per_sample - 1,
                            metadata_.mix_mask, metadata_.tdm.type == metadata::TdmType::I2s);
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
    uint32_t mdiv = metadata_.mClockDivFactor / ((frame_rate_ == 96000) ? 2 : 1);
    status = aml_audio_->SetMclkDiv(mdiv - 1);  // register val is div - 1;
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
      return status;
    }
    aml_audio_->SetMClkPad(MCLK_PAD_0);
  }
  if (metadata_.sClockDivFactor) {
    uint32_t frame_sync_clks = 0;
    switch (metadata_.tdm.type) {
      case metadata::TdmType::I2s:
      case metadata::TdmType::StereoLeftJustified:
        // For I2S and Stereo Left Justified we have a 50% duty cycle, hence the frame sync clocks
        // is set to the size of one slot.
        frame_sync_clks = metadata_.tdm.bits_per_slot;
        break;
      case metadata::TdmType::Tdm1:
        frame_sync_clks = 1;
        break;
    }
    status =
        aml_audio_->SetSclkDiv(metadata_.sClockDivFactor - 1, frame_sync_clks - 1,
                               (metadata_.tdm.bits_per_slot * metadata_.dai_number_of_channels) - 1,
                               !metadata_.tdm.sclk_on_raising);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
      return status;
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

// See //docs/concepts/drivers/driver_interfaces/audio_codec.md for a description of DAI terms used.
// See //src/lib/ddktl/include/ddktl/metadata/audio.h for descriptions of audio metadata.
// See //src/devices/lib/amlogic/include/soc/aml-common/aml-audio.h for descriptions of AMLogic
// specific metadata.
void AmlG12TdmStream::InitDaiFormats() {
  frame_rate_ = kMinSampleRate;
  for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
    // Only the PCM signed sample format is supported.
    dai_formats_[i].sample_format = SAMPLE_FORMAT_PCM_SIGNED;
    dai_formats_[i].frame_rate = frame_rate_;
    dai_formats_[i].bits_per_sample = metadata_.tdm.bits_per_sample;
    dai_formats_[i].bits_per_slot = metadata_.tdm.bits_per_slot;
    dai_formats_[i].number_of_channels = metadata_.dai_number_of_channels;
    dai_formats_[i].channels_to_use_bitmask = metadata_.codecs_channels_mask[i];
    switch (metadata_.tdm.type) {
      case metadata::TdmType::I2s:
        dai_formats_[i].frame_format = FRAME_FORMAT_I2S;
        break;
      case metadata::TdmType::StereoLeftJustified:
        dai_formats_[i].frame_format = FRAME_FORMAT_STEREO_LEFT;
        break;
      case metadata::TdmType::Tdm1:
        dai_formats_[i].frame_format = FRAME_FORMAT_TDM1;
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
  // Only the PCM signed sample format is supported.
  if (metadata_.tdm.sample_format != metadata::SampleFormat::PcmSigned) {
    zxlogf(ERROR, "%s metadata unsupported sample type %d", __FILE__,
           static_cast<int>(metadata_.tdm.sample_format));
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata_.tdm.type == metadata::TdmType::I2s ||
      metadata_.tdm.type == metadata::TdmType::StereoLeftJustified) {
    metadata_.dai_number_of_channels = 2;
  }
  if (metadata_.tdm.bits_per_sample == 0) {
    metadata_.tdm.bits_per_sample = 16;
  }
  if (metadata_.tdm.bits_per_slot == 0) {
    metadata_.tdm.bits_per_slot = 32;
  }
  if (metadata_.tdm.bits_per_slot != 32 && metadata_.tdm.bits_per_slot != 16) {
    zxlogf(ERROR, "%s metadata unsupported bits per slot %d", __FILE__,
           metadata_.tdm.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata_.tdm.bits_per_sample != 32 && metadata_.tdm.bits_per_sample != 16) {
    zxlogf(ERROR, "%s metadata unsupported bits per sample %d", __FILE__,
           metadata_.tdm.bits_per_sample);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata_.tdm.bits_per_sample > metadata_.tdm.bits_per_slot) {
    zxlogf(ERROR, "%s metadata unsupported bits per sample bits per slot combination %u/%u",
           __FILE__, metadata_.tdm.bits_per_sample, metadata_.tdm.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
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

    if (!IsDaiFormatSupported(dai_formats_[i], supported_formats.value())) {
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
  unique_id_ = metadata_.unique_id;
  const char* tdm_type = nullptr;
  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
      tdm_type = "i2s";
      break;
    case metadata::TdmType::StereoLeftJustified:
      tdm_type = "ljt";
      break;
    case metadata::TdmType::Tdm1:
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
  for (size_t i = 0; i < metadata_.tdm.number_of_external_delays; ++i) {
    if (metadata_.tdm.external_delays[i].frequency == req.frames_per_second) {
      external_delay_nsec_ = metadata_.tdm.external_delays[i].nsecs;
      break;
    }
  }
  if (req.frames_per_second != frame_rate_ || req.channels_to_use_bitmask != channels_to_use_) {
    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      // Put codecs in safe state for rate change
      auto status = codecs_[i].Stop();
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to stop the codec", __FILE__);
        return status;
      }
    }

    frame_rate_ = req.frames_per_second;
    for (size_t i = 0; i < metadata_.tdm.number_of_codecs; ++i) {
      dai_formats_[i].frame_rate = frame_rate_;
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
      if (status != ZX_OK) {
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
ZIRCON_DRIVER(aml_tdm, audio::aml_g12::driver_ops, "aml-tdm", "0.1")
