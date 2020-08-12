// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream.h"

#include <lib/zx/clock.h>
#include <math.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_call.h>

namespace audio {
namespace astro {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_ENABLE_GPIO,
  FRAGMENT_CODEC,
  FRAGMENT_COUNT,
};

constexpr size_t kMaxNumberOfChannels = 2;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
constexpr size_t kBytesPerSample = 2;
// Calculate ring buffer size for 1 second of 16-bit, max rate.
constexpr size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
    kMaxSampleRate * kBytesPerSample * kMaxNumberOfChannels, PAGE_SIZE);

AstroTdmStream::AstroTdmStream(zx_device_t* parent, bool is_input, ddk::PDev pdev,
                               const ddk::GpioProtocolClient enable_gpio)
    : SimpleAudioStream(parent, is_input),
      pdev_(std::move(pdev)),
      enable_gpio_(std::move(enable_gpio)) {
  dai_format_.number_of_channels = metadata_.number_of_channels;
  for (size_t i = 0; i < kMaxNumberOfChannels; ++i) {
    dai_format_.channels_to_use.push_back(1 << i);  // Use all channels.
  }
  dai_format_.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
  dai_format_.justify_format = JUSTIFY_FORMAT_JUSTIFY_I2S;
  dai_format_.frame_rate = kMinSampleRate;
  dai_format_.bits_per_sample = 16;
  dai_format_.bits_per_channel = 32;
}

zx_status_t AstroTdmStream::InitHW() {
  zx_status_t status;

  // Shut down the SoC audio peripherals (tdm/dma)
  aml_audio_->Shutdown();

  auto on_error = fbl::MakeAutoCall([this]() { aml_audio_->Shutdown(); });

  aml_audio_->Initialize();
  // Setup TDM.

  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
      // 4/3 bitoffset, 2 slots (regardless of number of channels), 32 bits/slot, 16 bits/sample.
      // Note: 3 bit offest places msb of sample one sclk period after edge of fsync
      // to provide i2s framing
      aml_audio_->ConfigTdmSlot(metadata_.is_input ? 4 : 3, 1, 31, 15, 0);
      switch (metadata_.number_of_channels) {
        case 1:
          // Lane 0, unmask first slot only (0x00000002),
          status = aml_audio_->ConfigTdmLane(metadata_.is_input ? 1 : 0, 0x00000002, 0);
          break;
        case 2:
          // L+R channels in lanes 0/1.
          aml_audio_->ConfigTdmSwaps(metadata_.is_input ? 0x00003200 : 0x00000010);
          // Lane 0/1, unmask 2 slots (0x00000003),
          status = aml_audio_->ConfigTdmLane(metadata_.is_input ? 1 : 0, 0x00000003, 0);
          break;
        default:
          zxlogf(ERROR, "%s Unsupported number of channels %d", __FILE__,
                 metadata_.number_of_channels);
          return ZX_ERR_NOT_SUPPORTED;
      }
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure TDM lane %d", __FILE__, status);
        return status;
      }
      break;
    case metadata::TdmType::Pcm:
      if (metadata_.number_of_channels != 1) {
        zxlogf(ERROR, "%s Unsupported number of channels %d", __FILE__,
               metadata_.number_of_channels);
        return ZX_ERR_NOT_SUPPORTED;
      }
      // bitoffset = 4/3, 1 slot, 16 bits/slot, 32 bits/sample.
      // For output bitoffest 3 places msb of sample one sclk period after fsync to provide PCM
      // framing.
      aml_audio_->ConfigTdmSlot(metadata_.is_input ? 4 : 3, 0, 31, 15, 0);

      if (metadata_.is_input) {
        aml_audio_->ConfigTdmSwaps(0x00000200);
      }
      // Lane 0/1, unmask first slot.
      status = aml_audio_->ConfigTdmLane(metadata_.is_input ? 1 : 0, 0x00000001, 0);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure TDM lane %d", __FILE__, status);
        return status;
      }
      break;
  }

  // PLL sourcing audio clock tree should be running at 768MHz
  // Note: Audio clock tree input should always be < 1GHz
  // mclk rate for 96kHz = 768MHz/5 = 153.6MHz
  // mclk rate for 48kHz = 768MHz/10 = 76.8MHz
  // Note: absmax mclk frequency is 500MHz per AmLogic
  uint32_t mdiv = (dai_format_.frame_rate == 96000) ? 5 : 10;
  status = aml_audio_->SetMclkDiv(mdiv - 1);  // register val is div - 1;
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
    return status;
  }

  // No need to set mclk pad via SetMClkPad (TAS2770 features "MCLK Free Operation").

  // 48kHz: sclk=76.8MHz/25 = 3.072MHz, 3.072MHz/64=48kkHz
  // 96kHz: sclk=153.6MHz/25 = 6.144MHz, 6.144MHz/64=96kHz
  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
      // lrduty = 32 sclk cycles (write 31) for i2s
      // invert sclk = true = sclk is rising edge in middle of bit for i2s
      status = aml_audio_->SetSclkDiv(24, 31, 63, true);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
        return status;
      }
      break;
    case metadata::TdmType::Pcm:
      // lrduty = 1 sclk cycles (write 0) for PCM
      // TODO(andresoportus): For now we set lrduty to 2 sclk cycles (write 1), 1 does not work.
      // invert sclk = false = sclk is falling edge in middle of bit for PCM
      status = aml_audio_->SetSclkDiv(24, 1, 31, false);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
        return status;
      }
      break;
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

zx_status_t AstroTdmStream::InitPDev() {
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

  dai_format_.number_of_channels = metadata_.number_of_channels;
  dai_format_.channels_to_use.clear();
  for (size_t i = 0; i < metadata_.number_of_channels; ++i) {
    dai_format_.channels_to_use.push_back(1 << i);  // Use all channels.
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  // Either we have all fragments (for I2S) or we have only one fragment (for PCM).
  if (metadata_.tdm.codec != metadata::Codec::None) {
    if (actual != countof(fragments)) {
      zxlogf(ERROR, "%s could not get the correct number of fragments with codec %lu", __FILE__,
             actual);
      return ZX_ERR_NOT_SUPPORTED;
    }
  } else {
    if (actual != 1) {
      zxlogf(ERROR, "%s could not get the correct number of fragments with no codec %lu", __FILE__,
             actual);
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti - %d", __func__, status);
    return status;
  }

  if (metadata_.tdm.codec != metadata::Codec::None) {
    status = codec_.SetProtocol(fragments[FRAGMENT_CODEC]);
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

  if (metadata_.tdm.codec != metadata::Codec::None) {
    auto info = codec_.GetInfo();
    if (info.is_error())
      return info.error_value();

    // Reset and initialize codec after we have configured I2S.
    status = codec_.Reset();
    if (status != ZX_OK) {
      return status;
    }

    auto supported_formats = codec_.GetDaiFormats();
    if (supported_formats.is_error()) {
      return supported_formats.error_value();
    }

    if (!codec_.IsDaiFormatSupported(dai_format_, supported_formats.value())) {
      zxlogf(ERROR, "%s codec does not support DAI format\n", __FILE__);
      return ZX_ERR_NOT_SUPPORTED;
    }

    status = codec_.SetDaiFormat(dai_format_);
    if (status != ZX_OK) {
      return status;
    }

    codec_.Start();
    if (status != ZX_OK) {
      return status;
    }
  }

  zxlogf(INFO, "audio: astro audio %s initialized", metadata_.is_input ? "input" : "output");
  return ZX_OK;
}

zx_status_t AstroTdmStream::Init() {
  zx_status_t status;

  status = InitPDev();
  if (status != ZX_OK) {
    return status;
  }

  status = AddFormats();
  if (status != ZX_OK) {
    return status;
  }

  // Set our gain capabilities.
  if (metadata_.tdm.codec != metadata::Codec::None) {
    auto gain = codec_.GetGainState();
    if (gain.is_error()) {
      return gain.error_value();
    }

    cur_gain_state_.cur_gain = gain->gain_db;
    cur_gain_state_.cur_mute = gain->muted;
    cur_gain_state_.cur_agc = gain->agc_enable;

    auto format = codec_.GetGainFormat();
    if (format.is_error()) {
      return format.error_value();
    }

    cur_gain_state_.min_gain = format->min_gain_db;
    cur_gain_state_.max_gain = format->max_gain_db;
    cur_gain_state_.gain_step = format->gain_step_db;
    cur_gain_state_.can_mute = format->can_mute;
    cur_gain_state_.can_agc = format->can_agc;
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

  switch (metadata_.tdm.type) {
    case metadata::TdmType::I2s:
      snprintf(device_name_, sizeof(device_name_), "astro-audio-i2s-%s",
               metadata_.is_input ? "in" : "out");
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
      break;
    case metadata::TdmType::Pcm:
      snprintf(device_name_, sizeof(device_name_), "astro-audio-pcm-%s",
               metadata_.is_input ? "in" : "out");
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_BT;
      break;
  }
  snprintf(mfr_name_, sizeof(mfr_name_), "Spacely Sprockets");
  snprintf(prod_name_, sizeof(prod_name_), "astro");

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

// Timer handler for sending out position notifications
void AstroTdmStream::ProcessRingNotification() {
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

zx_status_t AstroTdmStream::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = aml_audio_->fifo_depth();
  switch (metadata_.tdm.type) {  // TODO(andresoportus): Use product instead.
    case metadata::TdmType::I2s:
      // Report our external delay based on the chosen frame rate.  Note that these
      // delays were measured on Astro hardware, and should be pretty good, but they
      // will not be perfect.  One reason for this is that we are not taking any
      // steps to align our start time with start of a TDM frame, which will cause
      // up to 1 frame worth of startup error ever time that the output starts.
      // Also note that this is really nothing to worry about.  Hitting our target
      // to within 20.8uSec (for 48k) is pretty good.
      switch (req.frames_per_second) {
        case 48000:
          external_delay_nsec_ = ZX_USEC(125);
          break;

        case 96000:
          external_delay_nsec_ = ZX_NSEC(83333);
          break;

        default:
          return ZX_ERR_INVALID_ARGS;
      }
      break;
    case metadata::TdmType::Pcm:
      external_delay_nsec_ = 0;  // Unknown.
      break;
  }

  if (req.frames_per_second != dai_format_.frame_rate) {
    if (metadata_.tdm.codec != metadata::Codec::None) {
      // Put codec in safe state for rate change
      auto status = codec_.Stop();
      if (status != ZX_OK) {
        return status;
      }
    }

    uint32_t last_rate = dai_format_.frame_rate;
    dai_format_.frame_rate = req.frames_per_second;
    auto status = InitHW();
    if (status != ZX_OK) {
      dai_format_.frame_rate = last_rate;
      return status;
    }
    if (metadata_.tdm.codec != metadata::Codec::None) {
      status = codec_.SetDaiFormat(dai_format_);
      if (status != ZX_OK) {
        dai_format_.frame_rate = last_rate;
        return status;
      }

      // Restart codec
      return codec_.Start();
    }
  }

  return ZX_OK;
}

void AstroTdmStream::ShutdownHook() {
  if (metadata_.tdm.codec != metadata::Codec::None) {
    // safe the codec so it won't throw clock errors when tdm bus shuts down
    codec_.Stop();
  }
  if (enable_gpio_.is_valid()) {
    enable_gpio_.Write(0);
  }
  aml_audio_->Shutdown();
  pinned_ring_buffer_.Unpin();
}

zx_status_t AstroTdmStream::SetGain(const audio_proto::SetGainReq& req) {
  if (metadata_.tdm.codec != metadata::Codec::None) {
    // Modify parts of the gain state we have received in the request.
    GainState gain({.gain_db = req.gain,
                    .muted = cur_gain_state_.cur_mute,
                    .agc_enable = cur_gain_state_.cur_agc});
    if (req.flags & AUDIO_SGF_MUTE_VALID) {
      gain.muted = req.flags & AUDIO_SGF_MUTE;
    }
    if (req.flags & AUDIO_SGF_AGC_VALID) {
      gain.agc_enable = req.flags & AUDIO_SGF_AGC;
    };
    codec_.SetGainState(gain);

    // Update our gain state, with what is actually set in the codec.
    auto updated_gain = codec_.GetGainState();
    if (updated_gain.is_error()) {
      return updated_gain.error_value();
    }
    cur_gain_state_.cur_gain = updated_gain->gain_db;
    cur_gain_state_.cur_mute = updated_gain->muted;
    cur_gain_state_.cur_agc = updated_gain->agc_enable;
  }
  return ZX_OK;
}

zx_status_t AstroTdmStream::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

zx_status_t AstroTdmStream::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ =
        static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                              (frame_size_ * dai_format_.frame_rate / 1000 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  if (metadata_.tdm.codec != metadata::Codec::None) {
    // Restore mute to cur_gain_state_.cur_mute (we set it to true in Stop below).
    codec_.SetGainState({.gain_db = cur_gain_state_.cur_gain,
                         .muted = cur_gain_state_.cur_mute,
                         .agc_enable = cur_gain_state_.cur_agc});
  }
  return ZX_OK;
}

zx_status_t AstroTdmStream::Stop() {
  if (metadata_.tdm.codec != metadata::Codec::None) {
    // Set mute to true.
    codec_.SetGainState({.gain_db = cur_gain_state_.cur_gain,
                         .muted = true,
                         .agc_enable = cur_gain_state_.cur_agc});
  }
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t AstroTdmStream::AddFormats() {
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

zx_status_t AstroTdmStream::InitBuffer(size_t size) {
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
  // Either we have all fragments (for I2S) or we have only one fragment (for PCM).
  if (metadata.tdm.codec != metadata::Codec::None) {
    if (actual != countof(fragments)) {
      zxlogf(ERROR, "%s could not get the correct number of fragments with codec %lu", __FILE__,
             actual);
      return ZX_ERR_NOT_SUPPORTED;
    }
  } else {
    if (actual != 1) {
      zxlogf(ERROR, "%s could not get the correct number of fragments with no codec %lu", __FILE__,
             actual);
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  if (metadata.is_input) {
    auto stream = audio::SimpleAudioStream::Create<audio::astro::AstroTdmStream>(
        device, true, fragments[FRAGMENT_PDEV], fragments[FRAGMENT_ENABLE_GPIO] ?
        fragments[FRAGMENT_ENABLE_GPIO] : ddk::GpioProtocolClient());
    if (stream == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);
  } else {
    auto stream = audio::SimpleAudioStream::Create<audio::astro::AstroTdmStream>(
        device, false, fragments[FRAGMENT_PDEV], fragments[FRAGMENT_ENABLE_GPIO] ?
        fragments[FRAGMENT_ENABLE_GPIO] : ddk::GpioProtocolClient());
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

}  // namespace astro
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_tdm, audio::astro::driver_ops, "aml-tdm", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(aml_tdm)
    // clang-format on
