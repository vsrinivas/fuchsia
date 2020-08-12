// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-out.h"

#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_call.h>
#include <soc/aml-t931/t931-gpio.h>

namespace audio {
namespace sherlock {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_FAULT_GPIO,
  FRAGMENT_ENABLE_GPIO,
  FRAGMENT_I2C_0,
  FRAGMENT_I2C_1,
  FRAGMENT_I2C_2,  // Optional
  FRAGMENT_COUNT,
};

// Expects L+R for the 1 Woofer (mixed in HW) + L+R for tweeters.
// The user must perform crossover filtering on these channels.
constexpr size_t kNumberOfChannels = 4;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
// Calculate ring buffer size for 1 second of 16-bit, max rate.
constexpr size_t kRingBufferSize =
    fbl::round_up<size_t, size_t>(kMaxSampleRate * 2 * kNumberOfChannels, PAGE_SIZE);

SherlockAudioStreamOut::SherlockAudioStreamOut(zx_device_t* parent)
    : SimpleAudioStream(parent, false), pdev_(parent) {
  frames_per_second_ = kMinSampleRate;
}

zx_status_t SherlockAudioStreamOut::InitCodecs() {
  audio_en_.Write(1);  // Enable codecs by setting SOC_AUDIO_EN.

  auto status = codecs_[0]->Init(0, frames_per_second_);  // Use TDM slot 0.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to initialize codec 0", __FILE__);
    audio_en_.Write(0);
    return status;
  }
  status = codecs_[1]->Init(1, frames_per_second_);  // Use TDM slot 1.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to initialize codec 1", __FILE__);
    audio_en_.Write(0);
    return status;
  }
  status = codecs_[2]->Init(0, frames_per_second_);  // Use TDM slot 0.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to initialize codec 2", __FILE__);
    audio_en_.Write(0);
    return status;
  }

  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::InitHW() {
  aml_audio_->Shutdown();

  auto status = InitCodecs();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not init codecs - %d", __func__, status);
    return status;
  }

  auto on_error = fbl::MakeAutoCall([this]() { aml_audio_->Shutdown(); });

  aml_audio_->Initialize();

  // Setup Stereo Left Justified:
  // -lrclk duty = 64 sclk (SetSclkDiv lrdiv=63 below).
  // -No delay from the time the lrclk signal changes state state to the first bit of data on the
  // data lines.
  // -3072MHz/64 = 48KHz.

  // 5 bitoffset, 2 slots, 32 bits/slot, 16 bits/sample, enable mix L+R on lane 1.
  aml_audio_->ConfigTdmSlot(5, 1, 31, 15, (1 << 1));

  // Lane 0 L channel set to FRDDR slot 2.
  // Lane 0 R channel set to FRDDR slot 3.
  // Lane 1 L channel set to FRDDR slot 0. Mixed with R, see ConfigTdmOutSlot above.
  // Lane 1 R channel set to FRDDR slot 1. Mixed with L, see ConfigTdmOutSlot above.
  aml_audio_->ConfigTdmSwaps(0x00001032);

  // Tweeters: Lane 0, unmask TDM slots 0 & 1 (L+R FRDDR slots 2 & 3).
  uint32_t mute_slots = (channels_to_use_bitmask_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED)
                            ? (~static_cast<uint32_t>(channels_to_use_bitmask_)) & 3
                            : 0;
  status = aml_audio_->ConfigTdmLane(0, 0x00000003, mute_slots);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure TDM out lane0 %d", __FILE__, status);
    return status;
  }

  // Woofer: Lane 1, unmask TDM slot 0 & 1 (Woofer FRDDR slots 0 & 1).
  mute_slots = (channels_to_use_bitmask_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED)
                   ? ((~static_cast<uint32_t>(channels_to_use_bitmask_)) & 0xc) >> 2
                   : 0;
  status = aml_audio_->ConfigTdmLane(1, 0x00000003, mute_slots);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure TDM out lane1 %d", __FILE__, status);
    return status;
  }

  // mclk = T931_HIFI_PLL_RATE/125 = 1536MHz/125 = 12.288MHz.
  status = aml_audio_->SetMclkDiv(124);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
    return status;
  }

  // Per schematic, mclk uses pad 0 (MCLK_0 instead of MCLK_1).
  aml_audio_->SetMClkPad(MCLK_PAD_0);

  // for 48kHz: sclk = 12.288MHz/4 = 3.072MHz, 32L + 32R sclks = 64 sclks.
  // for 96kHz: sclk = 12.288MHz/2 = 6.144MHz, 32L + 32R sclks = 64 sclks.
  status = aml_audio_->SetSclkDiv((12'288'000 / (frames_per_second_ * 64)) - 1, 31, 63, false);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
    return status;
  }

  aml_audio_->Sync();

  on_error.cancel();
  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::InitPdev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol");
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  size_t actual;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  if (actual < countof(fragments) - 1) {
    zxlogf(ERROR, "could not get fragments");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = fragments[FRAGMENT_PDEV];
  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &codecs_types_,
                               sizeof(metadata::Codec), &actual);
  if (status != ZX_OK || sizeof(metadata::Codec) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  if (codecs_types_ == metadata::Codec::Tas5720x3) {
    zxlogf(INFO, "audio: using 3 Tas5720 codecs");
    fbl::AllocChecker ac;
    codecs_ = fbl::Array(new (&ac) std::unique_ptr<Tas5720>[3], 3);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < 3; ++i) {
      codecs_[i] = Tas5720::Create(fragments[FRAGMENT_I2C_0 + i]);
      if (!codecs_[i]) {
        zxlogf(ERROR, "%s could not get tas5720", __func__);
        return ZX_ERR_NO_RESOURCES;
      }
    }
  } else {
    zxlogf(ERROR, "%s invalid or unsupported codec", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  audio_fault_ = fragments[FRAGMENT_FAULT_GPIO];
  audio_en_ = fragments[FRAGMENT_ENABLE_GPIO];

  if (!audio_fault_.is_valid() || !audio_en_.is_valid()) {
    zxlogf(ERROR, "%s failed to allocate gpio", __func__);
    return ZX_ERR_NO_RESOURCES;
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
  aml_audio_ = AmlTdmOutDevice::Create(*std::move(mmio), HIFI_PLL, TDM_OUT_C, FRDDR_A, MCLK_C);
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "%s failed to create tdm device", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  // Drive strength settings
  status = pdev_.MapMmio(1, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  // Strength 1 for sclk (bit 14, GPIOZ(7)) and lrclk (bit 12, GPIOZ(6)),
  // GPIO offsets are in 4 bytes units.
  mmio->SetBits<uint32_t>((1 << 14) | (1 << 12), 4 * T931_PAD_DS_REG4A);
  status = pdev_.MapMmio(2, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  // Strength 1 for mclk (bit 18,  GPIOAO(9)), GPIO offsets are in 4 bytes units.
  mmio->SetBit<uint32_t>(18, 4 * T931_AO_PAD_DS_A);

  InitBuffer(kRingBufferSize);
  aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                        pinned_ring_buffer_.region(0).size);

  return InitHW();
}

zx_status_t SherlockAudioStreamOut::SetCodecsGain(float gain) {
  ZX_DEBUG_ASSERT(codecs_.size() == 3);

  // TODO(andresoportus): Get this param through product metadata.
  // Boost the woofer above tweeters by 7.1db analog and 5.5db digital needed for this product.
  constexpr float kDeltaGainWooferVsTweeters = 12.6f;
  auto status = codecs_[0]->SetGain(gain - kDeltaGainWooferVsTweeters);
  if (status != ZX_OK) {
    return status;
  }
  status = codecs_[1]->SetGain(gain - kDeltaGainWooferVsTweeters);
  if (status != ZX_OK) {
    return status;
  }
  status = codecs_[2]->SetGain(gain);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::Init() {
  zx_status_t status;

  status = InitPdev();
  if (status != ZX_OK) {
    return status;
  }

  status = AddFormats();
  if (status != ZX_OK) {
    return status;
  }

  float min_gain = codecs_[0]->GetMinGain();
  min_gain = std::max(min_gain, codecs_[1]->GetMinGain());
  min_gain = std::max(min_gain, codecs_[2]->GetMinGain());

  float max_gain = codecs_[0]->GetMaxGain();
  max_gain = std::min(max_gain, codecs_[1]->GetMaxGain());
  max_gain = std::min(max_gain, codecs_[2]->GetMaxGain());

  float gain_step = codecs_[0]->GetGainStep();
  gain_step = std::max(gain_step, codecs_[1]->GetGainStep());
  gain_step = std::max(gain_step, codecs_[2]->GetGainStep());

  // Use woofer as reference initial gain.
  float gain = codecs_[2]->GetGain();
  status = SetCodecsGain(gain);
  if (status != ZX_OK) {
    return status;
  }
  cur_gain_state_.cur_gain = gain;

  cur_gain_state_.cur_mute = false;
  cur_gain_state_.cur_agc = false;

  cur_gain_state_.min_gain = min_gain;
  cur_gain_state_.max_gain = max_gain;
  cur_gain_state_.gain_step = gain_step;
  cur_gain_state_.can_mute = false;
  cur_gain_state_.can_agc = false;

  snprintf(device_name_, sizeof(device_name_), "sherlock-audio-out");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "sherlock");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

// Timer handler for sending out position notifications.
void SherlockAudioStreamOut::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(us_per_notification_ != 0);

  // TODO(andresoportus): johngro noticed there is some drifting on notifications here,
  // could be improved with maintaining an absolute time and even better computing using
  // rationals, but higher level code should not rely on this anyways (see MTWN-57).
  notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = aml_audio_->GetRingPosition();
  if (resp.ring_buffer_pos >= rb_size_) {
    zxlogf(WARNING, "Ring buffer position 0x%X beyond ring buffer size 0x%X", resp.ring_buffer_pos,
           rb_size_);
    resp.ring_buffer_pos = 0;
  }
  NotifyPosition(resp);
}

zx_status_t SherlockAudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = aml_audio_->fifo_depth();
  external_delay_nsec_ = 0;

  if (req.frames_per_second != 48000 && req.frames_per_second != 96000) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (req.frames_per_second != frames_per_second_ ||
      (channels_to_use_bitmask_ != req.channels_to_use_bitmask)) {
    auto last_channels_to_use_bitmask = channels_to_use_bitmask_;
    channels_to_use_bitmask_ = req.channels_to_use_bitmask;
    auto last_rate = frames_per_second_;
    frames_per_second_ = req.frames_per_second;
    auto status = InitHW();
    if (status != ZX_OK) {
      frames_per_second_ = last_rate;
      channels_to_use_bitmask_ = last_channels_to_use_bitmask;
      return status;
    }

    // Set gain after the codec is reinitialized.
    SetCodecsGain(cur_gain_state_.cur_gain);
  }

  // At this time only one format is supported, and hardware is initialized
  // during driver binding, so nothing to do at this time.
  return ZX_OK;
}

void SherlockAudioStreamOut::ShutdownHook() {
  aml_audio_->Shutdown();
  audio_en_.Write(0);
  pinned_ring_buffer_.Unpin();
}

zx_status_t SherlockAudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
  auto status = SetCodecsGain(req.gain);
  if (status != ZX_OK) {
    return status;
  }
  cur_gain_state_.cur_gain = req.gain;
  // TODO(andresoportus): More options on volume setting, e.g.: Add codecs mute and fade support.
  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

  rb_size_ = rb_frames * frame_size_;
  aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, rb_size_);

  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::Start(uint64_t* out_start_time) {
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
  for (size_t i = 0; i < codecs_.size(); ++i) {
    auto status = codecs_[i]->Mute(false);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::Stop() {
  for (size_t i = 0; i < codecs_.size(); ++i) {
    auto status = codecs_[i]->Mute(true);
    if (status != ZX_OK) {
      return status;
    }
  }
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t SherlockAudioStreamOut::AddFormats() {
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

zx_status_t SherlockAudioStreamOut::InitBuffer(size_t size) {
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

  // TODO(ZX-3149): Per johngro's suggestion preallocate contiguous memory (say in
  // platform bus) since we are likely to fail after running for a while and we need to
  // init again (say the devhost is restarted).
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
  auto stream = audio::SimpleAudioStream::Create<audio::sherlock::SherlockAudioStreamOut>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = audio_bind;
  return ops;
}();

}  // namespace sherlock
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_sherlock_tdm, audio::sherlock::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(aml_sherlock_tdm)
    // clang-format on
