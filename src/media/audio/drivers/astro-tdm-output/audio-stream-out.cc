// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream-out.h"

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
  FRAGMENT_I2C,
  FRAGMENT_FAULT_GPIO,
  FRAGMENT_ENABLE_GPIO,
  FRAGMENT_COUNT,
};

constexpr size_t kNumberOfChannels = 1;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
constexpr size_t kBytesPerSample = 2;
// Calculate ring buffer size for 1 second of 16-bit, max rate.
constexpr size_t kRingBufferSize =
    fbl::round_up<size_t, size_t>(kMaxSampleRate * kBytesPerSample * kNumberOfChannels, PAGE_SIZE);

AstroAudioStreamOut::AstroAudioStreamOut(zx_device_t* parent) : SimpleAudioStream(parent, false) {
  frames_per_second_ = kMinSampleRate;
}

zx_status_t AstroAudioStreamOut::InitHW() {
  zx_status_t status;

  // Shut down the SoC audio peripherals (tdm/dma)
  aml_audio_->Shutdown();

  auto on_error = fbl::MakeAutoCall([this]() { aml_audio_->Shutdown(); });

  aml_audio_->Initialize();
  // Setup TDM.

  switch (tdm_config_.type) {
    case metadata::TdmType::I2s:
      // 3 bitoffset, 2 slots, 32 bits/slot, 16 bits/sample.
      // Note: 3 bit offest places msb of sample one sclk period after edge of fsync
      // to provide i2s framing
      aml_audio_->ConfigTdmOutSlot(3, 1, 31, 15, 0);

      // Lane 0, unmask first slot only (0x00000002),
      status = aml_audio_->ConfigTdmOutLane(0, 0x00000002, 0);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure TDM out lane %d", __FILE__, status);
        return status;
      }
      break;
    case metadata::TdmType::Pcm:
      // bitoffset = 3, 1 slot, 16 bits/slot, 16 bits/sample.
      // bitoffest = 3 places msb of sample one sclk period after fsync to provide PCM framing.
      aml_audio_->ConfigTdmOutSlot(3, 0, 15, 15, 0);

      // Lane 0, unmask first slot1 (0x00000001),
      status = aml_audio_->ConfigTdmOutLane(0, 0x00000001, 0);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not configure TDM out lane %d", __FILE__, status);
        return status;
      }
      break;
  }

  // PLL sourcing audio clock tree should be running at 768MHz
  // Note: Audio clock tree input should always be < 1GHz
  // mclk rate for 96kHz = 768MHz/5 = 153.6MHz
  // mclk rate for 48kHz = 768MHz/10 = 76.8MHz
  // Note: absmax mclk frequency is 500MHz per AmLogic
  uint32_t mdiv = (frames_per_second_ == 96000) ? 5 : 10;
  status = aml_audio_->SetMclkDiv(mdiv - 1);  // register val is div - 1;
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
    return status;
  }

  // No need to set mclk pad via SetMClkPad (TAS2770 features "MCLK Free Operation").

  // 48kHz: sclk=76.8MHz/25 = 3.072MHz, 3.072MHz/64=48kkHz
  // 96kHz: sclk=153.6MHz/25 = 6.144MHz, 6.144MHz/64=96kHz
  switch (tdm_config_.type) {
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
      status = aml_audio_->SetSclkDiv(24, 1, 15, false);
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

zx_status_t AstroAudioStreamOut::InitPDev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol");
    return status;
  }

  size_t actual = 0;
  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &tdm_config_,
                               sizeof(metadata::Tdm), &actual);
  if (status != ZX_OK || sizeof(metadata::Tdm) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  // Either we have all fragments (for I2S) or we have only one fragment (for PCM).
  switch (tdm_config_.type) {
    case metadata::TdmType::I2s:
      if (actual != countof(fragments)) {
        zxlogf(ERROR, "could not get the correct number of fragments for I2S %lu", actual);
        return ZX_ERR_NOT_SUPPORTED;
      }
      break;
    case metadata::TdmType::Pcm:
      if (actual != 1) {
        zxlogf(ERROR, "could not get the correct number of fragments for PCM %lu", actual);
        return ZX_ERR_NOT_SUPPORTED;
      }
      break;
  }

  pdev_ = fragments[FRAGMENT_PDEV];
  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  switch (tdm_config_.type) {
    case metadata::TdmType::I2s: {
      ddk::GpioProtocolClient audio_fault = fragments[FRAGMENT_FAULT_GPIO];
      ddk::GpioProtocolClient audio_en = fragments[FRAGMENT_ENABLE_GPIO];

      if (!audio_fault.is_valid() || !audio_en.is_valid()) {
        zxlogf(ERROR, "%s failed to allocate gpio\n", __func__);
        return ZX_ERR_NO_RESOURCES;
      }

      ddk::I2cChannel i2c = fragments[FRAGMENT_I2C];
      if (!i2c.is_valid()) {
        zxlogf(ERROR, "%s failed to allocate i2c", __func__);
        return ZX_ERR_NO_RESOURCES;
      }

      codec_ =
          Tas27xx::Create(std::move(i2c), std::move(audio_en), std::move(audio_fault), true, true);
      if (!codec_) {
        zxlogf(ERROR, "%s could not get tas27xx", __func__);
        return ZX_ERR_NO_RESOURCES;
      }
    } break;
    case metadata::TdmType::Pcm:
      // No codec for PCM.
      break;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti - %d", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    return status;
  }

  switch (tdm_config_.type) {
    case metadata::TdmType::I2s:
      aml_audio_ = AmlTdmDevice::Create(*std::move(mmio), HIFI_PLL, TDM_OUT_B, FRDDR_B, MCLK_B);
      if (aml_audio_ == nullptr) {
        zxlogf(ERROR, "%s failed to create TDM device", __func__);
        return ZX_ERR_NO_MEMORY;
      }
      break;
    case metadata::TdmType::Pcm:
      aml_audio_ = AmlTdmDevice::Create(*std::move(mmio), HIFI_PLL, TDM_OUT_A, FRDDR_A, MCLK_A);
      if (aml_audio_ == nullptr) {
        zxlogf(ERROR, "%s failed to create PCM device", __func__);
        return ZX_ERR_NO_MEMORY;
      }
      break;
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

  if (tdm_config_.codec != metadata::Codec::None) {
    status = codec_->Init(frames_per_second_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not initialize tas27xx - %d\n", __func__, status);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::Init() {
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
  if (tdm_config_.codec != metadata::Codec::None) {
    float gain;
    status = codec_->GetGain(&gain);
    if (status != ZX_OK) {
      return status;
    }

    cur_gain_state_.cur_gain = gain;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;

    cur_gain_state_.min_gain = codec_->GetMinGain();
    cur_gain_state_.max_gain = codec_->GetMaxGain();
    cur_gain_state_.gain_step = codec_->GetGainStep();
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;
  } else {
    cur_gain_state_.cur_gain = 1.f;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;

    cur_gain_state_.min_gain = 1.f;
    cur_gain_state_.max_gain = 1.f;
    cur_gain_state_.gain_step = .0f;
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;
  }

  switch (tdm_config_.type) {
    case metadata::TdmType::I2s:
      snprintf(device_name_, sizeof(device_name_), "astro-audio-i2s-out");
      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
      break;
    case metadata::TdmType::Pcm:
      snprintf(device_name_, sizeof(device_name_), "astro-audio-pcm-out");
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
void AstroAudioStreamOut::ProcessRingNotification() {
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

zx_status_t AstroAudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = aml_audio_->fifo_depth();
  switch (tdm_config_.type) {
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

  if (req.frames_per_second != frames_per_second_) {
    // Put codec in safe state for rate change
    zx_status_t status = codec_->Stop();
    if (status != ZX_OK) {
      return status;
    }

    uint32_t last_rate = frames_per_second_;
    frames_per_second_ = req.frames_per_second;
    status = InitHW();
    if (status != ZX_OK) {
      frames_per_second_ = last_rate;
      return status;
    }
    // Note: autorate is enabled in the coded, so changine codec rate
    // is not required.

    // Restart codec
    return codec_->Start();
  }

  return ZX_OK;
}

void AstroAudioStreamOut::ShutdownHook() {
  // safe the codec so it won't throw clock errors when tdm bus shuts down
  codec_->HardwareShutdown();
  aml_audio_->Shutdown();
}

zx_status_t AstroAudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
  zx_status_t status = codec_->SetGain(req.gain);
  if (status != ZX_OK) {
    return status;
  }
  float gain;
  status = codec_->GetGain(&gain);
  if (status == ZX_OK) {
    cur_gain_state_.cur_gain = gain;
  }

  return status;
}

zx_status_t AstroAudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

zx_status_t AstroAudioStreamOut::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ =
        static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                              (frame_size_ * frames_per_second_ / 1000 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  codec_->Mute(false);
  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::Stop() {
  codec_->Mute(true);
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  // Add the range for basic audio support.
  audio_stream_format_range_t range;

  range.min_channels = kNumberOfChannels;
  range.max_channels = kNumberOfChannels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::InitBuffer(size_t size) {
  zx_status_t status;
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
  auto stream = audio::SimpleAudioStream::Create<audio::astro::AstroAudioStreamOut>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);

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
ZIRCON_DRIVER_BEGIN(aml_tdm, audio::astro::driver_ops, "aml-tdm-out", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(aml_tdm)
    // clang-format on
